#ifndef SF_WEB_PORT_RENDERER_H
#define SF_WEB_PORT_RENDERER_H

#include <stdint.h>

#include "sf_web_renderer.h"

#define SF_WEB_PORT_COMMAND_CAPACITY 16384u

void sf_web_port_renderer_initialise(void);
void sf_web_port_renderer_begin_frame(void);
const SFWebRenderQuad *sf_web_port_renderer_command_buffer(void);
uint32_t sf_web_port_renderer_command_count(void);
uint32_t sf_web_port_renderer_command_capacity(void);

#endif
