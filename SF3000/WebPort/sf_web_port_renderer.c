#include "sf_web_port_renderer.h"
#include "sf_3do_compat.h"

static SFWebRenderQuad sf_web_port_commands[SF_WEB_PORT_COMMAND_CAPACITY];

void sf_web_port_renderer_initialise(void)
{
	sf3do_release_queued_text();
	sf_web_renderer_initialise(
		sf_web_port_commands,
		SF_WEB_PORT_COMMAND_CAPACITY
	);
}

void sf_web_port_renderer_begin_frame(void)
{
	sf3do_release_queued_text();
	sf_web_renderer_reset();
}

const SFWebRenderQuad *sf_web_port_renderer_command_buffer(void)
{
	return sf_web_renderer_commands();
}

uint32_t sf_web_port_renderer_command_count(void)
{
	return sf_web_renderer_command_count();
}

uint32_t sf_web_port_renderer_command_capacity(void)
{
	return sf_web_renderer_command_capacity();
}
