#include "sf_3do_compat.h"
#include "sf_web_runtime.h"
#include "sf_web_renderer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SF3DO_ITEM_FILE = 1,
    SF3DO_ITEM_IOREQ,
    SF3DO_ITEM_MSGPORT,
    SF3DO_ITEM_MESSAGE,
    SF3DO_ITEM_THREAD
};

#define SF3DO_MAX_ITEMS 128
#define SF3DO_PATH_CAPACITY 512
#define SF3DO_FRAMEBUFFER_BYTES (320u * 240u * 2u)
#define SF3DO_BLOCK_SIZE 2048u
#define SF3DO_LEGACY_FORMAT_SIZE 128u

typedef struct Sf3doFile {
    FILE *stream;
    FileStatus status;
    uint8 *data;
    size_t capacity;
    int is_nvram;
    int is_nvram_directory;
    char nvram_name[SF3DO_PATH_CAPACITY];
} Sf3doFile;

typedef struct Sf3doIoReq {
    IOReq request;
} Sf3doIoReq;

typedef struct Sf3doDirectory {
    Directory directory;
    uint32 index;
} Sf3doDirectory;

typedef struct Sf3doItem {
    Item item;
    int kind;
    void *value;
} Sf3doItem;

static Sf3doItem sf3do_items[SF3DO_MAX_ITEMS];
static Item sf3do_next_item = 1;
#if defined(__EMSCRIPTEN__)
static char sf3do_asset_root[SF3DO_PATH_CAPACITY] = "/";
#else
static char sf3do_asset_root[SF3DO_PATH_CAPACITY] = ".";
#endif
static int sf3do_asset_root_set;
static uint32 sf3do_pad_state;
static SignalMask sf3do_allocated_signals;
static SignalMask sf3do_pending_signals;
static uint64 sf3do_frames;
static Task sf3do_current_task = {{1}, 0};
static KernelBaseStruct sf3do_kernel_base = {&sf3do_current_task};
KernelBaseStruct *KernelBase = &sf3do_kernel_base;
static char **sf3do_queued_text;
static size_t sf3do_queued_text_count;
static size_t sf3do_queued_text_capacity;
static ScreenContext *sf3do_screen_context;

static int32 sf3do_screen_bank_for_item(Item item)
{
    int32 index;

    if (sf3do_screen_context == NULL) {
        return -1;
    }
    for (index = 0; index < sf3do_screen_context->sc_nScreens; ++index) {
        if (sf3do_screen_context->sc_BitmapItems[index] == item) {
            return index;
        }
    }
    return -1;
}

static int32 sf3do_screen_bank_for_buffer(const void *buffer)
{
    int32 index;

    if (sf3do_screen_context == NULL) {
        return -1;
    }
    for (index = 0; index < sf3do_screen_context->sc_nScreens; ++index) {
        if (sf3do_screen_context->sc_Bitmaps[index] != NULL &&
            sf3do_screen_context->sc_Bitmaps[index]->bm_Buffer == buffer) {
            return index;
        }
    }
    return -1;
}

static void sf3do_fill_vram(void *destination, uint32 value, size_t bytes)
{
    uint8 *output = (uint8 *)destination;
    size_t offset;

    for (offset = 0; offset + sizeof(value) <= bytes; offset += sizeof(value)) {
        memcpy(output + offset, &value, sizeof(value));
    }
    if (offset < bytes) {
        memcpy(output + offset, &value, bytes - offset);
    }
}

static uint32 sf3do_read_be32(const uint8 *data)
{
    return ((uint32)data[0] << 24) | ((uint32)data[1] << 16) |
           ((uint32)data[2] << 8) | (uint32)data[3];
}

static uint32 sf3do_font_char_width(const FontDescriptor *font, uint8 character)
{
    uint32 offset;

    if (font == NULL || font->fd_Data == NULL ||
        character < font->fd_FirstChar || character > font->fd_LastChar) {
        return 0u;
    }
    offset = font->fd_CharInfoOffset +
             ((uint32)character - font->fd_FirstChar) * 4u;
    if (offset > font->fd_Size || font->fd_Size - offset < 4u) {
        return 0u;
    }
    return sf3do_read_be32(font->fd_Data + offset) & 0xffu;
}

static uint32 sf3do_font_string_width(const FontDescriptor *font,
                                      const char *text)
{
    uint32 width = 0u;
    uint32 widest = 0u;
    uint32 characters = 0u;

    if (font == NULL || text == NULL) {
        return 0u;
    }
    while (*text != '\0') {
        uint8 character = (uint8)*text++;

        if (character == '\n' || character == '\r') {
            if (characters > 0u) {
                width += (characters - 1u) * font->fd_CharExtra;
            }
            if (width > widest) {
                widest = width;
            }
            width = 0u;
            characters = 0u;
            continue;
        }
        width += sf3do_font_char_width(font, character);
        ++characters;
    }
    if (characters > 0u) {
        width += (characters - 1u) * font->fd_CharExtra;
    }
    return width > widest ? width : widest;
}

static uint32 sf3do_font_string_height(const FontDescriptor *font,
                                       const char *text)
{
    uint32 lines = 1u;

    if (font == NULL || font->fd_CharHeight == 0u || text == NULL ||
        text[0] == '\0') {
        return 0u;
    }
    while (*text != '\0') {
        if (*text++ == '\n') {
            ++lines;
        }
    }
    return lines * font->fd_CharHeight + (lines - 1u) * font->fd_Leading;
}

static int sf3do_reserve_queued_text(void)
{
    char **text;
    size_t capacity;

    if (sf3do_queued_text_count < sf3do_queued_text_capacity) {
        return 1;
    }
    capacity = sf3do_queued_text_capacity == 0u ? 32u :
               sf3do_queued_text_capacity * 2u;
    if (capacity < sf3do_queued_text_capacity) {
        return 0;
    }
    text = (char **)realloc(sf3do_queued_text, capacity * sizeof(*text));
    if (text == NULL) {
        return 0;
    }
    sf3do_queued_text = text;
    sf3do_queued_text_capacity = capacity;
    return 1;
}

void sf3do_release_queued_text(void)
{
    size_t index;

    for (index = 0u; index < sf3do_queued_text_count; ++index) {
        free(sf3do_queued_text[index]);
    }
    sf3do_queued_text_count = 0u;
}

static void sf3do_append_text(const FontDescriptor *font, const char *text,
                              uint32 colour, int32 x, int32 y, uint32 width,
                              uint32 height)
{
    SFWebRenderQuad command = {0};
    char *snapshot;
    size_t bytes;

    if (text == NULL || text[0] == '\0' || width == 0u || height == 0u) {
        return;
    }

    bytes = strlen(text) + 1u;
    snapshot = (char *)malloc(bytes);
    if (snapshot == NULL || !sf3do_reserve_queued_text()) {
        free(snapshot);
        fprintf(stderr, "Unable to queue text for rendering.\n");
        return;
    }
    memcpy(snapshot, text, bytes);
    command.source = (uint32)(uintptr_t)snapshot;
    command.palette = colour;
    command.x[0] = x;
    command.x[1] = x + (int32)width;
    command.x[2] = x + (int32)width;
    command.x[3] = x;
    command.y[0] = y;
    command.y[1] = y;
    command.y[2] = y + (int32)height;
    command.y[3] = y + (int32)height;
    command.shade = 10;
    command.width = width;
    command.height = height;
    command.blend = SF_WEB_RENDER_BLEND_MIX;
    command.encoding = SF_WEB_RENDER_ENCODING_TEXT;
    if (sf_web_renderer_append(&command) < 0) {
        free(snapshot);
        fprintf(stderr, "Text render command queue is full.\n");
        return;
    }
    sf3do_queued_text[sf3do_queued_text_count++] = snapshot;
}

static FILE *sf3do_open_read_only(const char *path)
{
#if defined(_MSC_VER)
    FILE *file = NULL;

    return (fopen_s(&file, path, "rb") == 0) ? file : NULL;
#else
    return fopen(path, "rb");
#endif
}

static Sf3doItem *sf3do_find_item(Item item)
{
    size_t index;

    for (index = 0; index < SF3DO_MAX_ITEMS; ++index) {
        if (sf3do_items[index].item == item) {
            return &sf3do_items[index];
        }
    }

    return NULL;
}

static Item sf3do_add_item(int kind, void *value)
{
    size_t index;

    for (index = 0; index < SF3DO_MAX_ITEMS; ++index) {
        if (sf3do_items[index].item == 0) {
            sf3do_items[index].item = sf3do_next_item++;
            sf3do_items[index].kind = kind;
            sf3do_items[index].value = value;
            return sf3do_items[index].item;
        }
    }

    return SF3DO_ERR_IO;
}

static int sf3do_copy_string(char *destination, size_t capacity, const char *source)
{
    size_t length;

    if (destination == NULL || source == NULL || capacity == 0u) {
        return 0;
    }

    length = strlen(source);
    if (length >= capacity) {
        return 0;
    }

    memcpy(destination, source, length + 1u);
    return 1;
}

static int sf3do_path_is_safe(const char *path)
{
    const char *component = path;
    const char *cursor = path;

    while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
            if ((size_t)(cursor - component) == 2u &&
                component[0] == '.' && component[1] == '.') {
                return 0;
            }
            component = cursor + 1;
        } else if (*cursor == ':') {
            return 0;
        }
        ++cursor;
    }

    return !((size_t)(cursor - component) == 2u &&
             component[0] == '.' && component[1] == '.');
}

static Sf3doFile *sf3do_get_file(Item item)
{
    Sf3doItem *entry = sf3do_find_item(item);

    if (entry == NULL || entry->kind != SF3DO_ITEM_FILE) {
        return NULL;
    }

    return (Sf3doFile *)entry->value;
}

static Sf3doIoReq *sf3do_get_io_req(Item item)
{
    Sf3doItem *entry = sf3do_find_item(item);

    if (entry == NULL || entry->kind != SF3DO_ITEM_IOREQ) {
        return NULL;
    }

    return (Sf3doIoReq *)entry->value;
}

static int sf3do_is_nvram_path(const char *path)
{
    static const char nvram_root[] = "/NVRAM";

    return path != NULL &&
           strncmp(path, nvram_root, sizeof(nvram_root) - 1u) == 0 &&
           (path[sizeof(nvram_root) - 1u] == '\0' ||
            path[sizeof(nvram_root) - 1u] == '/' ||
            path[sizeof(nvram_root) - 1u] == '\\');
}

static const char *sf3do_nvram_name(const char *path)
{
    const char *name = path + sizeof("/NVRAM") - 1u;

    while (*name == '/' || *name == '\\') {
        ++name;
    }
    return name;
}

static int sf3do_resize_nvram_file(Sf3doFile *file, size_t capacity)
{
    uint8 *data;

    if (capacity <= file->capacity) {
        return 1;
    }
    data = (uint8 *)realloc(file->data, capacity);
    if (data == NULL) {
        return 0;
    }
    memset(data + file->capacity, 0, capacity - file->capacity);
    file->data = data;
    file->capacity = capacity;
    return 1;
}

#if defined(__EMSCRIPTEN__)
static int32 sf3do_open_nvram_file(const char *path, Item *item,
                                   FileStatus *status)
{
    Sf3doFile *file;
    const char *name;
    int32 size;
    Item file_item;

    file = (Sf3doFile *)calloc(1u, sizeof(*file));
    if (file == NULL) {
        return SF3DO_ERR_IO;
    }
    file->is_nvram = 1;
    name = sf3do_nvram_name(path);
    if (*name == '\0') {
        file->is_nvram_directory = 1;
    } else if (!sf3do_copy_string(file->nvram_name, sizeof(file->nvram_name),
                                  name)) {
        free(file);
        return SF3DO_ERR_INVALID;
    } else {
        size = sf_web_runtime_nvram_size(name);
        if (size < 0) {
            free(file);
            return SF3DO_ERR_NOT_FOUND;
        }
        if (!sf3do_resize_nvram_file(file, (size_t)size) ||
            sf_web_runtime_nvram_load(name, file->data, (uint32)size) != size) {
            free(file->data);
            free(file);
            return SF3DO_ERR_IO;
        }
        file->status.fs_ByteCount = size;
    }
    file->status.fs.ds_DeviceBlockSize = SF3DO_BLOCK_SIZE;
    file_item = sf3do_add_item(SF3DO_ITEM_FILE, file);
    if (file_item < 0) {
        free(file->data);
        free(file);
        return file_item;
    }
    *item = file_item;
    *status = file->status;
    return 0;
}
#endif

static int32 sf3do_open_file(const char *path, Item *item, FileStatus *status)
{
    char translated[SF3DO_PATH_CAPACITY];
    Sf3doFile *file;
    long length;
    Item file_item;

    if (item == NULL || status == NULL) {
        return SF3DO_ERR_INVALID;
    }
    if (sf3do_is_nvram_path(path)) {
#if !defined(__EMSCRIPTEN__)
        return SF3DO_ERR_UNSUPPORTED;
#else
        return sf3do_open_nvram_file(path, item, status);
#endif
    }
    if (sf_3do_translate_path(path, translated, sizeof(translated)) != 0) {
        return SF3DO_ERR_INVALID;
    }

    file = (Sf3doFile *)calloc(1u, sizeof(*file));
    if (file == NULL) {
        return SF3DO_ERR_IO;
    }

    file->stream = sf3do_open_read_only(translated);
    if (file->stream == NULL) {
        free(file);
        return SF3DO_ERR_NOT_FOUND;
    }

    if (fseek(file->stream, 0L, SEEK_END) != 0) {
        (void)fclose(file->stream);
        free(file);
        return SF3DO_ERR_IO;
    }

    length = ftell(file->stream);
    if (length < 0L || (uint64)length > INT32_MAX ||
        fseek(file->stream, 0L, SEEK_SET) != 0) {
        (void)fclose(file->stream);
        free(file);
        return SF3DO_ERR_IO;
    }

    file->status.fs.ds_DeviceBlockSize = SF3DO_BLOCK_SIZE;
    file->status.fs_ByteCount = (int32)length;
    file_item = sf3do_add_item(SF3DO_ITEM_FILE, file);
    if (file_item < 0) {
        (void)fclose(file->stream);
        free(file);
        return file_item;
    }

    *item = file_item;
    *status = file->status;
    return 0;
}

int32 sf_3do_set_asset_root(const char *root)
{
    if (root == NULL || root[0] == '\0' ||
        !sf3do_copy_string(sf3do_asset_root, sizeof(sf3do_asset_root), root)) {
        return SF3DO_ERR_INVALID;
    }

    sf3do_asset_root_set = 1;
    return 0;
}

const char *sf_3do_get_asset_root(void)
{
    if (!sf3do_asset_root_set) {
#if defined(_MSC_VER)
        char environment_root[SF3DO_PATH_CAPACITY];
        size_t required = 0u;

        if (getenv_s(&required, environment_root, sizeof(environment_root),
                     "SF_WEB_ASSET_ROOT") == 0 &&
            required > 1u && required <= sizeof(environment_root)) {
            (void)sf_3do_set_asset_root(environment_root);
        }
#else
        const char *environment_root;

        environment_root = getenv("SF_WEB_ASSET_ROOT");
        if (environment_root != NULL && environment_root[0] != '\0') {
            (void)sf_3do_set_asset_root(environment_root);
        }
#endif
    }

    return sf3do_asset_root;
}

int32 sf_3do_translate_path(const char *path, char *output, size_t output_size)
{
    static const char resource_prefix[] = "$boot/SF_Resources/";
    const char *relative_path;
    const char *root;
    size_t root_length;
    size_t relative_length;
    size_t position;

    if (path == NULL || output == NULL || output_size == 0u) {
        return SF3DO_ERR_INVALID;
    }

    relative_path = path;
    if (strncmp(path, resource_prefix, sizeof(resource_prefix) - 1u) == 0) {
        relative_path = path + sizeof(resource_prefix) - 1u;
    }
    while (*relative_path == '/' || *relative_path == '\\') {
        ++relative_path;
    }

    if (!sf3do_path_is_safe(relative_path)) {
        return SF3DO_ERR_INVALID;
    }

    root = sf_3do_get_asset_root();
    root_length = strlen(root);
    relative_length = strlen(relative_path);
    if (root_length + 1u + relative_length >= output_size) {
        return SF3DO_ERR_INVALID;
    }

    memcpy(output, root, root_length);
    position = root_length;
    if (position != 0u && output[position - 1u] != '/' &&
        output[position - 1u] != '\\') {
        output[position++] = '/';
    }
    memcpy(output + position, relative_path, relative_length + 1u);
    return 0;
}

void sf_3do_set_control_pad_state(uint32 buttons)
{
    sf3do_pad_state = buttons;
}

uint32 sf_3do_get_control_pad_state(void)
{
    return sf3do_pad_state;
}

uint64 sf_3do_frame_count(void)
{
    return sf3do_frames;
}

void *AllocMem(int32 size, uint32 mem_type)
{
    size_t allocation_size;

    (void)mem_type;
    if (size < 0) {
        return NULL;
    }

    allocation_size = (size == 0) ? 1u : (size_t)size;
    return calloc(1u, allocation_size);
}

void FreeMem(void *memory, int32 size)
{
    (void)size;
    free(memory);
}

void *NewPtr(int32 size, uint32 mem_type)
{
    return AllocMem(size, mem_type);
}

void FreePtr(void *memory)
{
    free(memory);
}

int32 OpenBlockFile(const char *path, BlockFile *block_file)
{
    Item item;
    int32 result;

    if (block_file == NULL) {
        return SF3DO_ERR_INVALID;
    }

    memset(block_file, 0, sizeof(*block_file));
    result = sf3do_open_file(path, &item, &block_file->fStatus);
    if (result != 0) {
        return result;
    }

    block_file->fDevice = item;
    return 0;
}

void CloseBlockFile(BlockFile *block_file)
{
    if (block_file != NULL && block_file->fDevice > 0) {
        (void)DeleteItem(block_file->fDevice);
        memset(block_file, 0, sizeof(*block_file));
    }
}

Item CreateBlockFileIOReq(Item device, Item reply_port)
{
    return CreateIOReq(NULL, 0, device, reply_port);
}

int32 AsynchReadBlockFile(BlockFile *block_file, Item io_req, void *buffer,
                          int32 length, int32 offset)
{
    Sf3doIoReq *request = sf3do_get_io_req(io_req);
    Sf3doFile *file;
    size_t read_count;

    if (block_file == NULL || request == NULL || buffer == NULL || length < 0 ||
        offset < 0 || request->request.ior_Device != block_file->fDevice) {
        return SF3DO_ERR_INVALID;
    }

    file = sf3do_get_file(block_file->fDevice);
    if (file == NULL) {
        request->request.ior_Result = SF3DO_ERR_IO;
        return request->request.ior_Result;
    }
    if (file->is_nvram) {
        if (offset > file->status.fs_ByteCount ||
            length > file->status.fs_ByteCount - offset) {
            request->request.ior_Result = SF3DO_ERR_IO;
            return request->request.ior_Result;
        }
        memcpy(buffer, file->data + offset, (size_t)length);
        request->request.ior_Result = 0;
        return request->request.ior_Result;
    }
    if (fseek(file->stream, (long)offset, SEEK_SET) != 0) {
        request->request.ior_Result = SF3DO_ERR_IO;
        return request->request.ior_Result;
    }

    read_count = fread(buffer, 1u, (size_t)length, file->stream);
    if (read_count < (size_t)length) {
        if (ferror(file->stream) != 0) {
            request->request.ior_Result = SF3DO_ERR_IO;
            return request->request.ior_Result;
        }

        /*
         * 3DO block-file reads include the padded tail block. Preserve that
         * contract for legacy callers that copy only the file's true size.
         */
        memset((unsigned char *)buffer + read_count, 0,
               (size_t)length - read_count);
    }
    request->request.ior_Result = 0;
    return request->request.ior_Result;
}

int32 WaitReadDoneBlockFile(Item io_req)
{
    Sf3doIoReq *request = sf3do_get_io_req(io_req);

    return (request == NULL) ? SF3DO_ERR_INVALID : request->request.ior_Result;
}

Item OpenDiskFile(const char *path)
{
    Item item;
    FileStatus status;

    if (path == NULL) {
        return SF3DO_ERR_UNSUPPORTED;
    }

    return (sf3do_open_file(path, &item, &status) == 0) ? item : SF3DO_ERR_NOT_FOUND;
}

int32 CloseDiskFile(Item file)
{
    return DeleteItem(file);
}

Item CreateFile(const char *path)
{
    const char *name;

    if (sf3do_is_nvram_path(path)) {
        name = sf3do_nvram_name(path);
        if (*name == '\0' || sf_web_runtime_nvram_store(name, NULL, 0u) != 0) {
            return SF3DO_ERR_IO;
        }
        return 1;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DeleteFile(const char *path)
{
    const char *name;

    if (sf3do_is_nvram_path(path)) {
        name = sf3do_nvram_name(path);
        if (*name == '\0') {
            return SF3DO_ERR_INVALID;
        }
        return sf_web_runtime_nvram_delete(name) == 0 ? 0 : SF3DO_ERR_IO;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

Directory *OpenDirectoryItem(Item directory)
{
    Sf3doFile *file = sf3do_get_file(directory);
    Sf3doDirectory *result;

    if (file == NULL || !file->is_nvram_directory) {
        return NULL;
    }
    result = (Sf3doDirectory *)calloc(1u, sizeof(*result));
    if (result != NULL) {
        result->directory.dir_Item = directory;
    }
    return result == NULL ? NULL : &result->directory;
}

int32 ReadDirectory(Directory *directory, DirectoryEntry *entry)
{
    Sf3doDirectory *state = (Sf3doDirectory *)directory;
    int32 length;

    if (state == NULL || entry == NULL) {
        return SF3DO_ERR_INVALID;
    }
    length = sf_web_runtime_nvram_list(state->index, entry->de_FileName,
                                       sizeof(entry->de_FileName));
    if (length < 0) {
        return SF3DO_ERR_NOT_FOUND;
    }
    ++state->index;
    return 0;
}

void CloseDirectory(Directory *directory)
{
    free(directory);
}

Item CreateIOReq(const char *name, int32 priority, Item device, Item reply_port)
{
    Sf3doIoReq *request;
    Item item;

    (void)name;
    (void)priority;
    (void)reply_port;
    if (device > 0 && sf3do_get_file(device) == NULL) {
        return SF3DO_ERR_INVALID;
    }

    request = (Sf3doIoReq *)calloc(1u, sizeof(*request));
    if (request == NULL) {
        return SF3DO_ERR_IO;
    }
    request->request.ior_Device = device;
    item = sf3do_add_item(SF3DO_ITEM_IOREQ, request);
    if (item < 0) {
        free(request);
    }
    return item;
}

int32 DeleteIOReq(Item io_req)
{
    return DeleteItem(io_req);
}

int32 DoIO(Item io_req, IOInfo *info)
{
    Sf3doIoReq *request = sf3do_get_io_req(io_req);
    Sf3doFile *file;
    size_t read_count;
    size_t offset;
    size_t length;

    if (request == NULL || info == NULL) {
        return SF3DO_ERR_INVALID;
    }
    file = sf3do_get_file(request->request.ior_Device);
    if (file == NULL) {
        return SF3DO_ERR_UNSUPPORTED;
    }
    if (file->is_nvram) {
        if (file->is_nvram_directory) {
            return SF3DO_ERR_UNSUPPORTED;
        }
        if (info->ioi_Command == CMD_STATUS) {
            if (info->ioi_Recv.iob_Buffer == NULL ||
                info->ioi_Recv.iob_Len < (int32)sizeof(FileStatus)) {
                return SF3DO_ERR_INVALID;
            }
            memcpy(info->ioi_Recv.iob_Buffer, &file->status, sizeof(file->status));
            return 0;
        }
        if (info->ioi_Offset < 0) {
            return SF3DO_ERR_INVALID;
        }
        offset = (size_t)info->ioi_Offset;
        if (info->ioi_Command == FILECMD_ALLOCBLOCKS) {
            if (offset > SIZE_MAX / SF3DO_BLOCK_SIZE ||
                !sf3do_resize_nvram_file(file, offset * SF3DO_BLOCK_SIZE)) {
                return SF3DO_ERR_IO;
            }
            return 0;
        }
        if (info->ioi_Command == FILECMD_SETEOF) {
            if (offset > file->capacity ||
                sf_web_runtime_nvram_store(file->nvram_name, file->data,
                                           (uint32)offset) != 0) {
                return SF3DO_ERR_IO;
            }
            file->status.fs_ByteCount = (int32)offset;
            return 0;
        }
        if (info->ioi_Command == CMD_READ) {
            if (info->ioi_Recv.iob_Buffer == NULL || info->ioi_Recv.iob_Len < 0) {
                return SF3DO_ERR_INVALID;
            }
            length = (size_t)info->ioi_Recv.iob_Len;
            if (offset > (size_t)file->status.fs_ByteCount ||
                length > (size_t)file->status.fs_ByteCount - offset) {
                return SF3DO_ERR_IO;
            }
            memcpy(info->ioi_Recv.iob_Buffer, file->data + offset, length);
            return 0;
        }
        if (info->ioi_Command == CMD_WRITE) {
            if (info->ioi_Send.iob_Buffer == NULL || info->ioi_Send.iob_Len < 0) {
                return SF3DO_ERR_INVALID;
            }
            length = (size_t)info->ioi_Send.iob_Len;
            if (length > SIZE_MAX - offset ||
                !sf3do_resize_nvram_file(file, offset + length)) {
                return SF3DO_ERR_IO;
            }
            memcpy(file->data + offset, info->ioi_Send.iob_Buffer, length);
            if (offset + length > (size_t)file->status.fs_ByteCount) {
                file->status.fs_ByteCount = (int32)(offset + length);
            }
            return 0;
        }
        return SF3DO_ERR_UNSUPPORTED;
    }

    if (info->ioi_Command == CMD_STATUS) {
        if (info->ioi_Recv.iob_Buffer == NULL ||
            info->ioi_Recv.iob_Len < (int32)sizeof(FileStatus)) {
            return SF3DO_ERR_INVALID;
        }
        memcpy(info->ioi_Recv.iob_Buffer, &file->status, sizeof(file->status));
        return 0;
    }
    if (info->ioi_Command != CMD_READ || info->ioi_Recv.iob_Buffer == NULL ||
        info->ioi_Recv.iob_Len < 0 || info->ioi_Offset < 0 ||
        fseek(file->stream, (long)info->ioi_Offset, SEEK_SET) != 0) {
        return SF3DO_ERR_UNSUPPORTED;
    }

    read_count = fread(info->ioi_Recv.iob_Buffer, 1u,
                       (size_t)info->ioi_Recv.iob_Len, file->stream);
    return (read_count == (size_t)info->ioi_Recv.iob_Len) ? 0 : SF3DO_ERR_IO;
}

void *LookupItem(Item item)
{
    Sf3doItem *entry = sf3do_find_item(item);

    return (entry == NULL) ? NULL : entry->value;
}

int32 InitEventUtility(int32 max_pads, int32 flags, int32 category)
{
    (void)max_pads;
    (void)flags;
    (void)category;
    return 0;
}

void KillEventUtility(void)
{
    sf3do_pad_state = 0;
}

int32 GetControlPad(int32 pad, Boolean wait, ControlPadEventData *data)
{
    (void)pad;
    (void)wait;
    if (data == NULL) {
        return SF3DO_ERR_INVALID;
    }

    data->cped_ButtonBits = sf3do_pad_state | sf_web_runtime_control_pad_state();
    return 0;
}

int32 AllocSignal(int32 requested_signal)
{
    SignalMask candidate;
    unsigned int bit;

    if (requested_signal != 0) {
        candidate = (SignalMask)requested_signal;
        if ((sf3do_allocated_signals & candidate) != 0u) {
            return SF3DO_ERR_IO;
        }
    } else {
        candidate = 0u;
        for (bit = 0u; bit < 31u; ++bit) {
            SignalMask test = ((SignalMask)1u << bit);
            if ((sf3do_allocated_signals & test) == 0u) {
                candidate = test;
                break;
            }
        }
        if (candidate == 0u) {
            return SF3DO_ERR_IO;
        }
    }

    sf3do_allocated_signals |= candidate;
    sf3do_current_task.t_AllocatedSigs = sf3do_allocated_signals;
    return (int32)candidate;
}

int32 FreeSignal(int32 signal)
{
    SignalMask mask = (SignalMask)signal;

    sf3do_allocated_signals &= ~mask;
    sf3do_pending_signals &= ~mask;
    sf3do_current_task.t_AllocatedSigs = sf3do_allocated_signals;
    return 0;
}

int32 SendSignal(Item task, SignalMask signals)
{
    (void)task;
    sf3do_pending_signals |= signals;
    return 0;
}

SignalMask WaitSignal(SignalMask signals)
{
    SignalMask received = sf3do_pending_signals & signals;

    sf3do_pending_signals &= ~received;
    return received;
}

Item CreateThread(const char *name, int32 priority, void (*entry)(void), int32 stack_size)
{
    void *thread;
    Item item;

    (void)name;
    (void)priority;
    (void)entry;
    (void)stack_size;
    thread = calloc(1u, 1u);
    if (thread == NULL) {
        return SF3DO_ERR_IO;
    }
    item = sf3do_add_item(SF3DO_ITEM_THREAD, thread);
    if (item < 0) {
        free(thread);
    }
    return item;
}

int32 DeleteThread(Item thread)
{
    return DeleteItem(thread);
}

Item CreateMsgPort(const char *name, int32 priority, int32 signal)
{
    MsgPort *port;
    Item item;
    int32 allocated_signal;

    (void)name;
    (void)priority;
    port = (MsgPort *)calloc(1u, sizeof(*port));
    if (port == NULL) {
        return SF3DO_ERR_IO;
    }
    allocated_signal = (signal == 0) ? AllocSignal(0) : signal;
    if (allocated_signal < 0) {
        free(port);
        return allocated_signal;
    }
    port->mp_Signal = (SignalMask)allocated_signal;
    item = sf3do_add_item(SF3DO_ITEM_MSGPORT, port);
    if (item < 0) {
        (void)FreeSignal(allocated_signal);
        free(port);
    }
    return item;
}

int32 DeleteMsgPort(Item port)
{
    return DeleteItem(port);
}

Item CreateMsg(const char *name, int32 priority, Item reply_port)
{
    Message *message;
    Item item;

    (void)name;
    (void)priority;
    (void)reply_port;
    message = (Message *)calloc(1u, sizeof(*message));
    if (message == NULL) {
        return SF3DO_ERR_IO;
    }
    item = sf3do_add_item(SF3DO_ITEM_MESSAGE, message);
    if (item < 0) {
        free(message);
    }
    return item;
}

int32 DeleteMsg(Item message)
{
    return DeleteItem(message);
}

Item CreateItem(int32 node_id, const TagArg *tags)
{
    (void)node_id;
    (void)tags;
    return CreateMsg(NULL, 0, 0);
}

int32 DeleteItem(Item item)
{
    Sf3doItem *entry = sf3do_find_item(item);

    if (entry == NULL) {
        return SF3DO_ERR_INVALID;
    }

    if (entry->kind == SF3DO_ITEM_FILE) {
        Sf3doFile *file = (Sf3doFile *)entry->value;

        if (file->stream != NULL) {
            (void)fclose(file->stream);
        }
        free(file->data);
        free(file);
    } else if (entry->kind == SF3DO_ITEM_MSGPORT) {
        MsgPort *port = (MsgPort *)entry->value;
        (void)FreeSignal((int32)port->mp_Signal);
        free(port);
    } else {
        free(entry->value);
    }

    memset(entry, 0, sizeof(*entry));
    return 0;
}

int32 SendMsg(Item port, Item message, const void *data, int32 data_size)
{
    Sf3doItem *port_entry = sf3do_find_item(port);
    Message *message_data;

    (void)data_size;
    if (port_entry == NULL || port_entry->kind != SF3DO_ITEM_MSGPORT) {
        return SF3DO_ERR_NOT_FOUND;
    }
    message_data = (Message *)LookupItem(message);
    if (message_data == NULL) {
        return SF3DO_ERR_INVALID;
    }
    message_data->msg_Result = 0;
    message_data->msg_DataPtr = (void *)data;
    return 0;
}

Item GetMsg(Item port)
{
    (void)port;
    return 0;
}

int32 ReplyMsg(Item message, int32 result, const void *data, int32 data_size)
{
    Message *message_data = (Message *)LookupItem(message);

    (void)data_size;
    if (message_data == NULL) {
        return SF3DO_ERR_INVALID;
    }
    message_data->msg_Result = result;
    message_data->msg_DataPtr = (void *)data;
    return 0;
}

int32 WaitPort(Item port, Item message)
{
    (void)port;
    (void)message;
    return 0;
}

Item FindNamedItem(int32 node_id, const char *name)
{
    (void)node_id;
    (void)name;
    return SF3DO_ERR_NOT_FOUND;
}

void PrintfSysErr(Err error)
{
    (void)error;
}

int32 OpenAudioFolio(void)
{
    return 0;
}

void CloseAudioFolio(void)
{
}

int32 OpenGraphics(ScreenContext *screen_context, int32 screens)
{
    int32 index;

    if (screen_context == NULL || screens < 1 || screens > 2) {
        return 0;
    }

    memset(screen_context, 0, sizeof(*screen_context));
    sf3do_screen_context = screen_context;
    screen_context->sc_nScreens = screens;
    screen_context->sc_nFrameBufferPages =
        (int32)((SF3DO_FRAMEBUFFER_BYTES + SF3DO_BLOCK_SIZE - 1u) / SF3DO_BLOCK_SIZE);
    for (index = 0; index < screens; ++index) {
        screen_context->sc_Screens[index] = (Screen *)calloc(1u, sizeof(Screen));
        screen_context->sc_Bitmaps[index] = (Bitmap *)calloc(1u, sizeof(Bitmap));
        if (screen_context->sc_Screens[index] == NULL ||
            screen_context->sc_Bitmaps[index] == NULL) {
            CloseGraphics(screen_context);
            return 0;
        }
        screen_context->sc_Bitmaps[index]->bm_Buffer =
            calloc(1u, SF3DO_FRAMEBUFFER_BYTES);
        if (screen_context->sc_Bitmaps[index]->bm_Buffer == NULL) {
            CloseGraphics(screen_context);
            return 0;
        }
        screen_context->sc_Bitmaps[index]->bm_Width = 320;
        screen_context->sc_Bitmaps[index]->bm_Height = 240;
        screen_context->sc_Bitmaps[index]->bm_BytesPerRow = 640;
        screen_context->sc_BitmapItems[index] = sf3do_next_item++;
    }

    return 1;
}

void CloseGraphics(ScreenContext *screen_context)
{
    int32 index;

    if (screen_context == NULL) {
        return;
    }
    for (index = 0; index < 2; ++index) {
        if (screen_context->sc_Bitmaps[index] != NULL) {
            free(screen_context->sc_Bitmaps[index]->bm_Buffer);
            free(screen_context->sc_Bitmaps[index]);
        }
        free(screen_context->sc_Screens[index]);
    }
    if (sf3do_screen_context == screen_context) {
        sf3do_screen_context = NULL;
    }
    memset(screen_context, 0, sizeof(*screen_context));
}

Item GetVRAMIOReq(void)
{
    return CreateIOReq(NULL, 0, 0, 0);
}

Item GetVBLIOReq(void)
{
    return CreateIOReq(NULL, 0, 0, 0);
}

Item GetTimerIOReq(void)
{
    return CreateIOReq(NULL, 0, 0, 0);
}

int32 WaitIO(Item io_req)
{
    (void)io_req;
    return 0;
}

int32 WaitVBL(Item io_req, int32 fields)
{
    (void)io_req;
    if (fields > 0) {
        sf3do_frames += (uint32)fields;
    }
    sf_web_runtime_wait_vbl(fields);
    return 0;
}

int32 WaitVBLDefer(Item io_req, int32 fields)
{
    return WaitVBL(io_req, fields);
}

void EnableVAVG(Screen *screen)
{
    (void)screen;
}

void EnableHAVG(Screen *screen)
{
    (void)screen;
}

void SetCEControl(Item bitmap, uint32 control, uint32 mask)
{
    (void)bitmap;
    (void)control;
    (void)mask;
}

void CopyVRAMPages(Item io_req, void *destination, const void *source,
                   int32 pages, int32 flags)
{
    int32 bank;

    (void)io_req;
    (void)flags;
    if (destination != NULL && source != NULL && pages > 0) {
        memcpy(destination, source, (size_t)pages * SF3DO_BLOCK_SIZE);
        bank = sf3do_screen_bank_for_buffer(destination);
        if (bank >= 0) {
            sf_web_runtime_copy_vram((uint32)bank, source);
        }
    }
}

void SetVRAMPages(Item io_req, void *destination, uint32 value,
                  int32 pages, int32 flags)
{
    int32 bank;

    (void)io_req;
    (void)flags;
    if (destination == NULL || pages <= 0) {
        return;
    }
    sf3do_fill_vram(destination, value, (size_t)pages * SF3DO_BLOCK_SIZE);
    bank = sf3do_screen_bank_for_buffer(destination);
    if (bank >= 0) {
        sf_web_runtime_clear_bank((uint32)bank, value);
    }
}

void DrawCels(Item bitmap, CCB *ccb)
{
    size_t count = 0u;

    while (ccb != NULL && count < 256u) {
        int32 target_bank = sf3do_screen_bank_for_item(bitmap);
        int32 source_bank = sf3do_screen_bank_for_buffer(ccb->ccb_SourcePtr);
        TextCel *text_cel = (TextCel *)ccb->ccb_SourcePtr;

        if ((ccb->ccb_Flags & CCB_SKIP) == 0 && target_bank >= 0 &&
            source_bank >= 0) {
            sf_web_runtime_queue_screen_cel(
               (uint32)target_bank, (uint32)source_bank, ccb->ccb_XPos,
               ccb->ccb_YPos, ccb->ccb_HDX, ccb->ccb_VDY, ccb->ccb_PIXC,
               ccb->ccb_Flags
            );
        } else if ((ccb->ccb_Flags & CCB_SKIP) == 0 &&
            text_cel != NULL && text_cel->tc_CCB == ccb &&
            text_cel->tc_RenderText != NULL &&
            ((const char *)text_cel->tc_RenderText)[0] != '\0' &&
            ccb->ccb_Width > 0) {
            uint32 width = (uint32)ccb->ccb_Width;
            uint32 visible_width = ccb->ccb_PRE1 & UINT32_C(0x3ff);

            if (visible_width < width) {
                width = visible_width;
            }
            sf3do_append_text(
                text_cel->tc_Font,
                (const char *)text_cel->tc_RenderText,
                text_cel->tc_ForeColor,
                ccb->ccb_XPos / 65536,
                ccb->ccb_YPos / 65536,
                width,
                (uint32)ccb->ccb_Height
            );
        }
        if (ccb->ccb_Flags & CCB_LAST) {
            break;
        }
        ccb = ccb->ccb_NextPtr;
        ++count;
    }
}

void DisplayScreen(Screen *screen, int32 flags)
{
    (void)screen;
    (void)flags;
}

void SetScreenColor(Screen *screen, uint32 color)
{
    (void)screen;
    (void)color;
}

uint32 MakeCLUTColorEntry(int32 index, int32 red, int32 green, int32 blue)
{
    return ((uint32)(index & 0x1f) << 24) | ((uint32)(red & 0xff) << 16) |
           ((uint32)(green & 0xff) << 8) | (uint32)(blue & 0xff);
}

uint32 MakeRGB15(int32 red, int32 green, int32 blue)
{
    return ((uint32)(red & 0x1f) << 10) | ((uint32)(green & 0x1f) << 5) |
           (uint32)(blue & 0x1f);
}

void SetFGPen(GrafCon *graf_con, uint32 color)
{
    if (graf_con != NULL) {
        graf_con->gc_FGPen = (int32)color;
    }
}

void FillRect(Item bitmap, const GrafCon *graf_con, const Rect *rectangle)
{
    int32 bank;

    if (graf_con == NULL || rectangle == NULL) {
        return;
    }
    bank = sf3do_screen_bank_for_item(bitmap);
    if (bank >= 0) {
        sf_web_runtime_fill_rect((uint32)bank, (uint32)graf_con->gc_FGPen,
                                 rectangle->rect_XLeft, rectangle->rect_YTop,
                                 rectangle->rect_XRight, rectangle->rect_YBottom);
    }
}

void FadeToBlack(ScreenContext *screen_context, int32 frames)
{
    (void)screen_context;
    sf_web_runtime_fade_to_black(frames);
}

void FadeFromBlack(ScreenContext *screen_context, int32 frames)
{
    (void)screen_context;
    sf_web_runtime_fade_from_black(frames);
}

FontDescriptor *LoadFont(const char *path, uint32 mem_type)
{
    BlockFile block_file;
    FontDescriptor *font;
    Item io_request;
    uint8 *data;
    uint32 size;
    int32 result;

    (void)mem_type;
    result = OpenBlockFile(path, &block_file);
    if (result != 0 || block_file.fStatus.fs_ByteCount < 84) {
        return NULL;
    }
    size = (uint32)block_file.fStatus.fs_ByteCount;
    data = (uint8 *)AllocMem(size, MEMTYPE_ANY);
    if (data == NULL) {
        CloseBlockFile(&block_file);
        return NULL;
    }
    io_request = CreateBlockFileIOReq(block_file.fDevice, 0);
    if (io_request < 0) {
        FreeMem(data, size);
        CloseBlockFile(&block_file);
        return NULL;
    }
    result = AsynchReadBlockFile(&block_file, io_request, data,
                                 (int32)size, 0);
    (void)DeleteIOReq(io_request);
    CloseBlockFile(&block_file);
    if (result != 0 || sf3do_read_be32(data) != UINT32_C(0x464f4e54) ||
        sf3do_read_be32(data + 4) != size ||
        sf3do_read_be32(data + 24) != 5u) {
        FreeMem(data, size);
        return NULL;
    }
    font = (FontDescriptor *)calloc(1u, sizeof(*font));
    if (font == NULL) {
        FreeMem(data, size);
        return NULL;
    }
    font->fd_Data = data;
    font->fd_Size = size;
    font->fd_FontFlags = sf3do_read_be32(data + 12);
    font->fd_CharHeight = sf3do_read_be32(data + 16);
    font->fd_FirstChar = sf3do_read_be32(data + 28);
    font->fd_LastChar = sf3do_read_be32(data + 32);
    font->fd_CharExtra = sf3do_read_be32(data + 36);
    font->fd_Leading = sf3do_read_be32(data + 48);
    font->fd_CharInfoOffset = sf3do_read_be32(data + 52);
    font->fd_CharDataOffset = sf3do_read_be32(data + 60);
    if (font->fd_CharHeight == 0u || font->fd_FirstChar > font->fd_LastChar ||
        font->fd_CharInfoOffset > font->fd_Size ||
        font->fd_CharDataOffset > font->fd_Size) {
        FreeMem(data, font->fd_Size);
        free(font);
        return NULL;
    }
    sf_web_runtime_set_text_font(font->fd_Data, font->fd_Size);
    return font;
}

void UnloadFont(FontDescriptor *font)
{
    if (font != NULL) {
        FreeMem(font->fd_Data, font->fd_Size);
        free(font);
    }
}

TextCel *CreateTextCel(FontDescriptor *font, int32 width, int32 height, int32 flags)
{
    TextCel *text_cel;

    (void)font;
    text_cel = (TextCel *)calloc(1u, sizeof(*text_cel));
    if (text_cel == NULL) {
        return NULL;
    }
    text_cel->tc_CCB = (CCB *)calloc(1u, sizeof(*text_cel->tc_CCB));
    if (text_cel->tc_CCB == NULL) {
        free(text_cel);
        return NULL;
    }
    text_cel->tc_RenderText = calloc(1024u, 1u);
    if (text_cel->tc_RenderText == NULL) {
        free(text_cel->tc_CCB);
        free(text_cel);
        return NULL;
    }
    text_cel->tc_CCB->ccb_Flags = (uint32)flags;
    text_cel->tc_CCB->ccb_Width = width;
    text_cel->tc_CCB->ccb_Height = height;
    text_cel->tc_CCB->ccb_SourcePtr = (CelData *)text_cel;
    text_cel->tc_ForeColor = UINT32_C(0x7fff);
    text_cel->tc_Font = font;
    return text_cel;
}

void DeleteTextCel(TextCel *text_cel)
{
    if (text_cel != NULL) {
        free(text_cel->tc_CCB);
        free(text_cel->tc_RenderText);
        free(text_cel);
    }
}

void SetTextCelSize(TextCel *text_cel, int32 width, int32 height)
{
    if (text_cel != NULL && text_cel->tc_CCB != NULL) {
        text_cel->tc_CCB->ccb_Width = width;
        text_cel->tc_CCB->ccb_Height = height;
    }
}

void GetTextCelSize(const TextCel *text_cel, long *width, long *height)
{
    if (width != NULL) {
        *width = (text_cel == NULL || text_cel->tc_CCB == NULL) ? 0L :
                 (long)text_cel->tc_CCB->ccb_Width;
    }
    if (height != NULL) {
        *height = (text_cel == NULL || text_cel->tc_CCB == NULL) ? 0L :
                  (long)text_cel->tc_CCB->ccb_Height;
    }
}

void SetTextCelColor(TextCel *text_cel, int32 index, int32 color)
{
    (void)index;
    if (text_cel != NULL) {
        text_cel->tc_ForeColor = (uint32)color;
    }
}

void SetTextCelCoords(TextCel *text_cel, int32 x, int32 y)
{
    if (text_cel != NULL && text_cel->tc_CCB != NULL) {
        text_cel->tc_CCB->ccb_XPos = (int32)((int64)x * 65536);
        text_cel->tc_CCB->ccb_YPos = (int32)((int64)y * 65536);
    }
}

void UpdateTextInCel(TextCel *text_cel, Boolean redraw, const char *text)
{
    size_t width;

    (void)redraw;
    if (text_cel == NULL || text_cel->tc_CCB == NULL) {
        return;
    }
    if (text_cel->tc_RenderText == NULL) {
        return;
    }
    if (text == NULL) {
        ((char *)text_cel->tc_RenderText)[0] = '\0';
    } else {
        (void)snprintf((char *)text_cel->tc_RenderText, 1024u, "%s", text);
    }
    width = sf3do_font_string_width(text_cel->tc_Font, text);
    if (width > 1023u) {
        width = 1023u;
    }
    text_cel->tc_CCB->ccb_Width = (int32)width;
    text_cel->tc_CCB->ccb_Height =
        (int32)sf3do_font_string_height(text_cel->tc_Font, text);
    text_cel->tc_CCB->ccb_PRE1 =
        (text_cel->tc_CCB->ccb_PRE1 & ~((uint32)0x3ff)) | (uint32)width;
}

void DrawTextString(FontDescriptor *font, GrafCon *graf_con, Item bitmap,
                    const char *text)
{
    (void)bitmap;
    if (graf_con != NULL && text != NULL) {
        uint32 width = sf3do_font_string_width(font, text);
        uint32 height = sf3do_font_string_height(font, text);

        if (width > 1023u) {
            width = 1023u;
        }
        sf3do_append_text(font, text, (uint32)graf_con->gc_FGPen,
                           graf_con->gc_PenX, graf_con->gc_PenY,
                           width, height);
    }
}

ubyte *LoadImage(const char *path, void *buffer, void *vdl,
                 ScreenContext *screen_context)
{
    BlockFile block_file;
    void *image_buffer = buffer;
    int32 result;
    int32 image_size;
    Item io_request;
    int allocated_buffer = 0;

    (void)vdl;
    (void)screen_context;
    result = OpenBlockFile(path, &block_file);
    if (result != 0) {
        return NULL;
    }
    image_size = block_file.fStatus.fs_ByteCount;
    if (image_buffer == NULL) {
        image_buffer = AllocMem(image_size, MEMTYPE_ANY);
        allocated_buffer = 1;
    }
    if (image_buffer == NULL) {
        CloseBlockFile(&block_file);
        return NULL;
    }
    io_request = CreateBlockFileIOReq(block_file.fDevice, 0);
    if (io_request < 0) {
        if (allocated_buffer != 0) {
            FreeMem(image_buffer, image_size);
        }
        CloseBlockFile(&block_file);
        return NULL;
    }
    result = AsynchReadBlockFile(&block_file, io_request, image_buffer,
                                 block_file.fStatus.fs_ByteCount, 0);
    (void)DeleteIOReq(io_request);
    CloseBlockFile(&block_file);
    if (result != 0 && allocated_buffer != 0) {
        FreeMem(image_buffer, image_size);
        return NULL;
    }
    return (result == 0) ? (ubyte *)image_buffer : NULL;
}

int32 InitDataAcq(int32 streams)
{
    (void)streams;
    return SF3DO_ERR_UNSUPPORTED;
}

void CloseDataAcq(void)
{
}

int32 NewDataAcq(AcqContextPtr *context, const char *path, int32 priority)
{
    (void)path;
    (void)priority;
    if (context != NULL) {
        *context = NULL;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

void DisposeDataAcq(AcqContextPtr context)
{
    free(context);
}

int32 InitDataStreaming(int32 streams)
{
    (void)streams;
    return SF3DO_ERR_UNSUPPORTED;
}

void CloseDataStreaming(void)
{
}

int32 NewDataStream(DSStreamCBPtr *stream, DSDataBufPtr buffers,
                    int32 block_size, int32 priority, int32 message_count)
{
    (void)buffers;
    (void)block_size;
    (void)priority;
    (void)message_count;
    if (stream != NULL) {
        *stream = NULL;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

void DisposeDataStream(Item message, DSStreamCBPtr stream)
{
    (void)message;
    free(stream);
}

int32 DSConnect(Item message, void *reply, DSStreamCBPtr stream, Item supplier_port)
{
    (void)message;
    (void)reply;
    (void)stream;
    (void)supplier_port;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSSubscribe(Item message, void *reply, DSStreamCBPtr stream,
                  DSDataType data_type, Item subscriber_port)
{
    (void)message;
    (void)reply;
    (void)stream;
    (void)data_type;
    (void)subscriber_port;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSStartStream(Item message, void *reply, DSStreamCBPtr stream, int32 options)
{
    (void)message;
    (void)reply;
    (void)stream;
    (void)options;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSStopStream(Item message, void *reply, DSStreamCBPtr stream, int32 options)
{
    (void)message;
    (void)reply;
    (void)stream;
    (void)options;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSSetClock(DSStreamCBPtr stream, int32 clock)
{
    (void)stream;
    (void)clock;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSWaitEndOfStream(Item message, DSRequestMsg *request, DSStreamCBPtr stream)
{
    (void)message;
    (void)request;
    (void)stream;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSControl(Item message, void *reply, DSStreamCBPtr stream, uint32 type,
                int32 operation, void *control)
{
    (void)message;
    (void)reply;
    (void)stream;
    (void)type;
    (void)operation;
    (void)control;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 DSSetChannel(Item message, void *reply, DSStreamCBPtr stream, uint32 type,
                   int32 channel, int32 state)
{
    (void)message;
    (void)reply;
    (void)stream;
    (void)type;
    (void)channel;
    (void)state;
    return SF3DO_ERR_UNSUPPORTED;
}

int32 PollForMsg(Item port, void *message, void *data, void *unused, int32 *status)
{
    (void)port;
    (void)message;
    (void)data;
    (void)unused;
    if (status != NULL) {
        *status = SF3DO_ERR_UNSUPPORTED;
    }
    return 1;
}

Item NewMsgPort(const char *name)
{
    return CreateMsgPort(name, 0, 0);
}

void RemoveMsgPort(Item port)
{
    (void)DeleteMsgPort(port);
}

Item CreateMsgItem(Item port)
{
    return CreateMsg(NULL, 0, port);
}

void RemoveMsgItem(Item message)
{
    (void)DeleteMsg(message);
}

int32 InitSAudioSubscriber(void)
{
    return SF3DO_ERR_UNSUPPORTED;
}

void CloseSAudioSubscriber(void)
{
}

int32 NewSAudioSubscriber(SAudioContextPtr *context, DSStreamCBPtr stream,
                          int32 priority)
{
    (void)stream;
    (void)priority;
    if (context != NULL) {
        *context = NULL;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

void DisposeSAudioSubscriber(SAudioContextPtr context)
{
    free(context);
}

int32 InitCtrlSubscriber(void)
{
    return SF3DO_ERR_UNSUPPORTED;
}

void CloseCtrlSubscriber(void)
{
}

int32 NewCtrlSubscriber(CtrlContextPtr *context, DSStreamCBPtr stream, int32 priority)
{
    (void)stream;
    (void)priority;
    if (context != NULL) {
        *context = NULL;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

void DisposeCtrlSubscriber(CtrlContextPtr context)
{
    free(context);
}

int32 InitCPakSubscriber(void)
{
    return SF3DO_ERR_UNSUPPORTED;
}

void CloseCPakSubscriber(void)
{
}

int32 NewCPakSubscriber(CPakContextPtr *context, int32 channels, int32 priority)
{
    (void)channels;
    (void)priority;
    if (context != NULL) {
        *context = NULL;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

void DisposeCPakSubscriber(CPakContextPtr context)
{
    free(context);
}

int32 InitCPakCel(DSStreamCBPtr stream, CPakContextPtr context,
                  CPakRecPtr *channel, int32 channel_number, Boolean flush)
{
    (void)stream;
    (void)context;
    (void)channel_number;
    (void)flush;
    if (channel != NULL) {
        *channel = NULL;
    }
    return SF3DO_ERR_UNSUPPORTED;
}

void DestroyCPakCel(CPakContextPtr context, CPakRecPtr channel, int32 channel_number)
{
    (void)context;
    (void)channel_number;
    free(channel);
}

void DrawCPakToBuffer(CPakContextPtr context, CPakRecPtr channel, Bitmap *bitmap)
{
    (void)context;
    (void)channel;
    (void)bitmap;
}

void SendFreeCPakSignal(CPakContextPtr context)
{
    (void)context;
}

void FlushCPakChannel(CPakContextPtr context, CPakRecPtr channel, int32 flags)
{
    (void)context;
    (void)channel;
    (void)flags;
}

int sf_3do_vsprintf(char *destination, const char *format, va_list arguments)
{
#if defined(_MSC_VER)
    return vsprintf_s(destination, SF3DO_LEGACY_FORMAT_SIZE, format, arguments);
#else
    return vsprintf(destination, format, arguments);
#endif
}
