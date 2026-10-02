#ifndef SF_3DO_COMPAT_H
#define SF_3DO_COMPAT_H

/*
 * Small, host-side subset of the 3DO Portfolio interfaces used by SF3000.
 * It deliberately models data ownership and file I/O, but leaves rendering
 * to the WebPort renderer. The asset root is on-demand and need only contain
 * non-media gameplay assets; Music, Video, Voices, and Samples are not
 * preloaded or required by this layer.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef uint8_t ubyte;
typedef int32 Item;
typedef int32 Err;
typedef int32 Boolean;
typedef uint32 SignalMask;
typedef int32 frac16;
typedef uint32 ufrac16;
typedef uint32 CelData;

#define TRUE 1
#define FALSE 0
#define true 1
#define false 0

#define SF3DO_ERR_UNSUPPORTED ((int32)-1000)
#define SF3DO_ERR_NOT_FOUND ((int32)-1001)
#define SF3DO_ERR_IO ((int32)-1002)
#define SF3DO_ERR_INVALID ((int32)-1003)

#define MEMTYPE_ANY ((uint32)0)
#define MEMTYPE_CEL ((uint32)1)
#define ALLOCMEM(size, mem_type) AllocMem((size), (mem_type))

#define TAG_END ((uint32)0)
#define CREATEMSG_TAG_REPLYPORT ((uint32)1)
#define CREATEMSG_TAG_DATA_SIZE ((uint32)2)

#define CCB_LAST ((uint32)0x00000001)
#define CCB_SKIP ((uint32)0x00000002)
#define CCB_NPABS ((uint32)0x00000004)
#define CCB_SPABS ((uint32)0x00000008)
#define CCB_PPABS ((uint32)0x00000010)
#define CCB_LDSIZE ((uint32)0x00000020)
#define CCB_LDPRS ((uint32)0x00000040)
#define CCB_LDPLUT ((uint32)0x00000080)
#define CCB_YOXY ((uint32)0x00000100)
#define CCB_ACW ((uint32)0x00000200)
#define CCB_ACCW ((uint32)0x00000400)
#define CCB_ACE ((uint32)0x00000800)
#define CCB_CCBPRE ((uint32)0x00001000)
#define CCB_USEAV ((uint32)0x00002000)
#define CCB_BGND ((uint32)0x00004000)

#define ControlUp ((uint32)0x00000001)
#define ControlDown ((uint32)0x00000002)
#define ControlLeft ((uint32)0x00000004)
#define ControlRight ((uint32)0x00000008)
#define ControlA ((uint32)0x00000010)
#define ControlB ((uint32)0x00000020)
#define ControlC ((uint32)0x00000040)
#define ControlStart ((uint32)0x00000080)
#define ControlX ((uint32)0x00000100)
#define ControlLeftShift ((uint32)0x00000200)
#define ControlRightShift ((uint32)0x00000400)

#define StickFire ((uint32)0x00000001)
#define StickA ((uint32)0x00000002)
#define StickB ((uint32)0x00000004)
#define StickC ((uint32)0x00000008)
#define StickUp ((uint32)0x00000010)
#define StickDown ((uint32)0x00000020)
#define StickRight ((uint32)0x00000040)
#define StickLeft ((uint32)0x00000080)
#define StickPlay ((uint32)0x00000100)
#define StickStop ((uint32)0x00000200)
#define StickLeftShift ((uint32)0x00000400)
#define StickRightShift ((uint32)0x00000800)

#define CMD_READ ((int32)1)
#define CMD_WRITE ((int32)2)
#define CMD_STATUS ((int32)3)
#define FILECMD_ALLOCBLOCKS ((int32)100)
#define FILECMD_SETEOF ((int32)101)

#define LC_Observer ((int32)1)
#define LC_NoSeeUm ((int32)0)

typedef struct TagArg {
    uint32 ta_Tag;
    void *ta_Arg;
} TagArg;

typedef struct CCB {
    uint32 ccb_Flags;
    struct CCB *ccb_NextPtr;
    CelData *ccb_SourcePtr;
    void *ccb_PLUTPtr;
    int32 ccb_XPos;
    int32 ccb_YPos;
    int32 ccb_HDX;
    int32 ccb_HDY;
    int32 ccb_VDX;
    int32 ccb_VDY;
    int32 ccb_HDDX;
    int32 ccb_HDDY;
    uint32 ccb_PIXC;
    uint32 ccb_PRE0;
    uint32 ccb_PRE1;
    int32 ccb_Width;
    int32 ccb_Height;
} CCB;

typedef struct Bitmap {
    void *bm_Buffer;
    int32 bm_Width;
    int32 bm_Height;
    int32 bm_BytesPerRow;
} Bitmap;

typedef struct Screen {
    uint32 screen_Flags;
} Screen;

typedef struct ScreenContext {
    Screen *sc_Screens[2];
    Bitmap *sc_Bitmaps[2];
    Item sc_BitmapItems[2];
    int32 sc_nScreens;
    int32 sc_curScreen;
    int32 sc_nFrameBufferPages;
} ScreenContext;

typedef struct FontDescriptor FontDescriptor;

typedef struct TextCel {
    CCB *tc_CCB;
    void *tc_userData;
    uint32 tc_ForeColor;
    FontDescriptor *tc_Font;
    void *tc_RenderText;
} TextCel;

struct FontDescriptor {
    uint8 *fd_Data;
    uint32 fd_Size;
    uint32 fd_FontFlags;
    uint32 fd_CharHeight;
    uint32 fd_FirstChar;
    uint32 fd_LastChar;
    uint32 fd_CharExtra;
    uint32 fd_Leading;
    uint32 fd_CharInfoOffset;
    uint32 fd_CharDataOffset;
};

typedef struct GrafCon {
    int32 gc_BGPen;
    int32 gc_FGPen;
    int32 gc_PenX;
    int32 gc_PenY;
} GrafCon;

typedef struct Rect {
    int32 rect_XLeft;
    int32 rect_XRight;
    int32 rect_YTop;
    int32 rect_YBottom;
} Rect;

typedef struct ControlPadEventData {
    uint32 cped_ButtonBits;
} ControlPadEventData;

typedef struct StickEventData {
    int16 stk_HorizPosition;
    int16 stk_VertPosition;
    int16 stk_DepthPosition;
    uint32 stk_ButtonBits;
} StickEventData;

typedef struct IOBuf {
    void *iob_Buffer;
    int32 iob_Len;
} IOBuf;

typedef struct IOInfo {
    int32 ioi_Command;
    IOBuf ioi_Send;
    IOBuf ioi_Recv;
    int32 ioi_Offset;
} IOInfo;

typedef struct IOReq {
    Item ior_Device;
    int32 ior_Result;
} IOReq;

typedef struct DeviceStatus {
    uint32 ds_DeviceBlockSize;
} DeviceStatus;

typedef struct FileStatus {
    DeviceStatus fs;
    int32 fs_ByteCount;
} FileStatus;

typedef struct BlockFile {
    Item fDevice;
    FileStatus fStatus;
} BlockFile;

typedef struct Directory {
    Item dir_Item;
} Directory;

typedef struct DirectoryEntry {
    char de_FileName[256];
} DirectoryEntry;

typedef struct Message {
    int32 msg_Result;
    void *msg_DataPtr;
} Message;

typedef struct MsgPort {
    SignalMask mp_Signal;
} MsgPort;

typedef struct Node {
    Item n_Item;
} Node;

typedef struct Task {
    Node t;
    SignalMask t_AllocatedSigs;
} Task;

typedef struct KernelBaseStruct {
    Task *kb_CurrentTask;
} KernelBaseStruct;

extern KernelBaseStruct *KernelBase;

#define KERNELNODE ((int32)1)
#define MSGPORTNODE ((int32)2)
#define MESSAGENODE ((int32)3)
#define MKNODEID(a, b) ((((int32)(a)) << 16) | (int32)(b))

typedef struct EventBrokerHeader {
    int32 ebh_Flavor;
} EventBrokerHeader;

typedef struct EventFrame {
    uint32 ef_ByteCount;
    int32 ef_EventNumber;
    int32 ef_PodPosition;
    int32 ef_GenericPosition;
    void *ef_EventData;
} EventFrame;

typedef struct PodDescription {
    uint32 pod_Flags;
} PodDescription;

typedef struct PodDescriptionList {
    EventBrokerHeader pdl_Header;
    int32 pdl_PodCount;
    PodDescription pdl_Pod[1];
} PodDescriptionList;

typedef struct ConfigurationRequest {
    EventBrokerHeader cr_Header;
    int32 cr_Category;
    uint32 cr_TriggerMask[3];
    uint32 cr_CaptureMask[3];
    int32 cr_QueueMax;
} ConfigurationRequest;

#define EventPortName "eventbroker"
#define EB_DescribePods ((int32)1)
#define EB_DescribePodsReply ((int32)2)
#define EB_EventRecord ((int32)3)
#define EB_Configure ((int32)4)
#define POD_IsStick ((uint32)0x01)
#define POD_IsMouse ((uint32)0x02)
#define POD_IsControlPad ((uint32)0x04)
#define POD_IsGun ((uint32)0x08)
#define EVENTNUM_ControlPortChange ((int32)1)
#define EVENTNUM_EventQueueOverflow ((int32)2)
#define EVENTNUM_StickButtonPressed ((int32)3)
#define EVENTNUM_StickButtonReleased ((int32)4)
#define EVENTNUM_StickUpdate ((int32)5)
#define EVENTNUM_StickMoved ((int32)6)
#define EVENTBIT0_StickButtonPressed ((uint32)0x00000001)
#define EVENTBIT0_StickButtonReleased ((uint32)0x00000002)
#define EVENTBIT0_StickUpdate ((uint32)0x00000004)
#define EVENTBIT0_StickDataArrived ((uint32)0x00000008)
#define EVENTBIT0_StickMoved ((uint32)0x00000010)
#define EVENTBIT2_ControlPortChange ((uint32)0x00000001)

typedef struct DSDataBuf {
    struct DSDataBuf *next;
    struct DSDataBuf *permanentNext;
} DSDataBuf;

typedef DSDataBuf *DSDataBufPtr;
typedef uint32 DSDataType;

#define DS_HDR_MAX_PRELOADINST 16
#define DS_HDR_MAX_SUBSCRIBER 8
#define DS_STREAM_VERSION ((int32)1)
#define HEADER_CHUNK_TYPE ((uint32)0x48445220)
#define SOPT_FLUSH ((int32)1)
#define CHAN_ENABLED ((int32)1)

#define CHAR4LITERAL(a, b, c, d) \
    (((uint32)(uint8)(a) << 24) | ((uint32)(uint8)(b) << 16) | \
     ((uint32)(uint8)(c) << 8) | (uint32)(uint8)(d))

typedef struct DSHeaderSubs {
    uint32 subscriberType;
    int32 deltaPriority;
} DSHeaderSubs;

typedef DSHeaderSubs *DSHeaderSubsPtr;

typedef struct DSHeaderChunk {
    int32 headerVersion;
    int32 streamBlockSize;
    int32 streamBuffers;
    int32 streamerDeltaPri;
    int32 dataAcqDeltaPri;
    int32 numSubsMsgs;
    int32 audioClockChan;
    uint32 enableAudioChan;
    Item preloadInstList[DS_HDR_MAX_PRELOADINST];
    DSHeaderSubs subscriberList[DS_HDR_MAX_SUBSCRIBER];
} DSHeaderChunk;

typedef DSHeaderChunk *DSHeaderChunkPtr;

typedef struct AcqContext {
    Item requestPort;
} AcqContext;

typedef AcqContext *AcqContextPtr;

typedef struct DSStreamCB {
    Item requestPort;
} DSStreamCB;

typedef DSStreamCB *DSStreamCBPtr;

typedef struct SAudioContext {
    Item requestPort;
} SAudioContext;

typedef SAudioContext *SAudioContextPtr;

typedef struct CtrlContext {
    Item requestPort;
} CtrlContext;

typedef CtrlContext *CtrlContextPtr;

typedef struct CPakContext {
    Item requestPort;
} CPakContext;

typedef CPakContext *CPakContextPtr;

typedef struct CPakRec {
    int32 channel;
} CPakRec;

typedef CPakRec *CPakRecPtr;

typedef struct SAudioCtlBlock {
    struct {
        Item *tagListPtr;
    } loadTemplates;
    struct {
        int32 channelNumber;
    } clock;
} SAudioCtlBlock;

typedef struct DSRequestMsg {
    int32 reserved;
} DSRequestMsg;

#define kSAudioCtlOpLoadTemplates ((int32)1)
#define kSAudioCtlOpSetClockChan ((int32)2)
#define SA_22K_16B_M_SDX2 ((Item)1)

#define CHECK_DS_RESULT(name, result) ((void)(name), (void)(result))
#define ERR(args) ((void)0)

/* Maps $boot/SF_Resources paths to an on-demand non-media asset root. */
int32 sf_3do_set_asset_root(const char *root);
const char *sf_3do_get_asset_root(void);
int32 sf_3do_translate_path(const char *path, char *output, size_t output_size);
void sf_3do_set_control_pad_state(uint32 buttons);
uint32 sf_3do_get_control_pad_state(void);
uint64 sf_3do_frame_count(void);

void *AllocMem(int32 size, uint32 mem_type);
void FreeMem(void *memory, int32 size);
void *NewPtr(int32 size, uint32 mem_type);
void FreePtr(void *memory);

int32 OpenBlockFile(const char *path, BlockFile *block_file);
void CloseBlockFile(BlockFile *block_file);
Item CreateBlockFileIOReq(Item device, Item reply_port);
int32 AsynchReadBlockFile(BlockFile *block_file, Item io_req, void *buffer,
                          int32 length, int32 offset);
int32 WaitReadDoneBlockFile(Item io_req);
Item OpenDiskFile(const char *path);
int32 CloseDiskFile(Item file);
Item CreateFile(const char *path);
int32 DeleteFile(const char *path);
Directory *OpenDirectoryItem(Item directory);
int32 ReadDirectory(Directory *directory, DirectoryEntry *entry);
void CloseDirectory(Directory *directory);
Item CreateIOReq(const char *name, int32 priority, Item device, Item reply_port);
int32 DeleteIOReq(Item io_req);
int32 DoIO(Item io_req, IOInfo *info);
void *LookupItem(Item item);

int32 InitEventUtility(int32 max_pads, int32 flags, int32 category);
void KillEventUtility(void);
int32 GetControlPad(int32 pad, Boolean wait, ControlPadEventData *data);
int32 AllocSignal(int32 requested_signal);
int32 FreeSignal(int32 signal);
int32 SendSignal(Item task, SignalMask signals);
SignalMask WaitSignal(SignalMask signals);
Item CreateThread(const char *name, int32 priority, void (*entry)(void), int32 stack_size);
int32 DeleteThread(Item thread);

Item CreateMsgPort(const char *name, int32 priority, int32 signal);
int32 DeleteMsgPort(Item port);
Item CreateMsg(const char *name, int32 priority, Item reply_port);
int32 DeleteMsg(Item message);
Item CreateItem(int32 node_id, const TagArg *tags);
int32 DeleteItem(Item item);
int32 SendMsg(Item port, Item message, const void *data, int32 data_size);
Item GetMsg(Item port);
int32 ReplyMsg(Item message, int32 result, const void *data, int32 data_size);
int32 WaitPort(Item port, Item message);
Item FindNamedItem(int32 node_id, const char *name);
void PrintfSysErr(Err error);

int32 OpenAudioFolio(void);
void CloseAudioFolio(void);
int32 OpenGraphics(ScreenContext *screen_context, int32 screens);
void CloseGraphics(ScreenContext *screen_context);
Item GetVRAMIOReq(void);
Item GetVBLIOReq(void);
Item GetTimerIOReq(void);
int32 WaitIO(Item io_req);
int32 WaitVBL(Item io_req, int32 fields);
int32 WaitVBLDefer(Item io_req, int32 fields);
void EnableVAVG(Screen *screen);
void EnableHAVG(Screen *screen);
void SetCEControl(Item bitmap, uint32 control, uint32 mask);
void CopyVRAMPages(Item io_req, void *destination, const void *source,
                   int32 pages, int32 flags);
void SetVRAMPages(Item io_req, void *destination, uint32 value,
                  int32 pages, int32 flags);
void DrawCels(Item bitmap, CCB *ccb);
void sf3do_release_queued_text(void);
void DisplayScreen(Screen *screen, int32 flags);
void SetScreenColor(Screen *screen, uint32 color);
uint32 MakeCLUTColorEntry(int32 index, int32 red, int32 green, int32 blue);
uint32 MakeRGB15(int32 red, int32 green, int32 blue);
void SetFGPen(GrafCon *graf_con, uint32 color);
void FillRect(Item bitmap, const GrafCon *graf_con, const Rect *rectangle);
void FadeToBlack(ScreenContext *screen_context, int32 frames);
void FadeFromBlack(ScreenContext *screen_context, int32 frames);

FontDescriptor *LoadFont(const char *path, uint32 mem_type);
FontDescriptor *LoadFontData(uint8 *data, uint32 size);
void UnloadFont(FontDescriptor *font);
TextCel *CreateTextCel(FontDescriptor *font, int32 width, int32 height, int32 flags);
void DeleteTextCel(TextCel *text_cel);
void SetTextCelSize(TextCel *text_cel, int32 width, int32 height);
void GetTextCelSize(const TextCel *text_cel, long *width, long *height);
void SetTextCelColor(TextCel *text_cel, int32 index, int32 color);
void SetTextCelCoords(TextCel *text_cel, int32 x, int32 y);
void UpdateTextInCel(TextCel *text_cel, Boolean redraw, const char *text);
void DrawTextString(FontDescriptor *font, GrafCon *graf_con, Item bitmap,
                    const char *text);
ubyte *LoadImage(const char *path, void *buffer, void *vdl,
                 ScreenContext *screen_context);

int32 InitDataAcq(int32 streams);
void CloseDataAcq(void);
int32 NewDataAcq(AcqContextPtr *context, const char *path, int32 priority);
void DisposeDataAcq(AcqContextPtr context);
int32 InitDataStreaming(int32 streams);
void CloseDataStreaming(void);
int32 NewDataStream(DSStreamCBPtr *stream, DSDataBufPtr buffers,
                    int32 block_size, int32 priority, int32 message_count);
void DisposeDataStream(Item message, DSStreamCBPtr stream);
int32 DSConnect(Item message, void *reply, DSStreamCBPtr stream, Item supplier_port);
int32 DSSubscribe(Item message, void *reply, DSStreamCBPtr stream,
                  DSDataType data_type, Item subscriber_port);
int32 DSStartStream(Item message, void *reply, DSStreamCBPtr stream, int32 options);
int32 DSStopStream(Item message, void *reply, DSStreamCBPtr stream, int32 options);
int32 DSSetClock(DSStreamCBPtr stream, int32 clock);
int32 DSWaitEndOfStream(Item message, DSRequestMsg *request, DSStreamCBPtr stream);
int32 DSControl(Item message, void *reply, DSStreamCBPtr stream, uint32 type,
                int32 operation, void *control);
int32 DSSetChannel(Item message, void *reply, DSStreamCBPtr stream, uint32 type,
                   int32 channel, int32 state);
int32 PollForMsg(Item port, void *message, void *data, void *unused, int32 *status);
Item NewMsgPort(const char *name);
void RemoveMsgPort(Item port);
Item CreateMsgItem(Item port);
void RemoveMsgItem(Item message);
int32 InitSAudioSubscriber(void);
void CloseSAudioSubscriber(void);
int32 NewSAudioSubscriber(SAudioContextPtr *context, DSStreamCBPtr stream,
                          int32 priority);
void DisposeSAudioSubscriber(SAudioContextPtr context);
int32 InitCtrlSubscriber(void);
void CloseCtrlSubscriber(void);
int32 NewCtrlSubscriber(CtrlContextPtr *context, DSStreamCBPtr stream, int32 priority);
void DisposeCtrlSubscriber(CtrlContextPtr context);
int32 InitCPakSubscriber(void);
void CloseCPakSubscriber(void);
int32 NewCPakSubscriber(CPakContextPtr *context, int32 channels, int32 priority);
void DisposeCPakSubscriber(CPakContextPtr context);
int32 InitCPakCel(DSStreamCBPtr stream, CPakContextPtr context,
                  CPakRecPtr *channel, int32 channel_number, Boolean flush);
void DestroyCPakCel(CPakContextPtr context, CPakRecPtr channel, int32 channel_number);
void DrawCPakToBuffer(CPakContextPtr context, CPakRecPtr channel, Bitmap *bitmap);
void SendFreeCPakSignal(CPakContextPtr context);
void FlushCPakChannel(CPakContextPtr context, CPakRecPtr channel, int32 flags);

#ifdef __cplusplus
}
#endif

#endif
