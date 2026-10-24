$input a_position

// U1, the chamber into the two light views: fixed function, depth alone.

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	gl_Position = place(xmul(vec4(a_position, 1.0), 0));
}
