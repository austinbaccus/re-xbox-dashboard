$input a_position

// The lettering of the logo: the factor colour alone.

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	gl_Position = place(xmul(vec4(a_position, 1.0), 0));
}
