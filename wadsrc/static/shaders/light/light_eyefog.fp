varying float fogcoord;
uniform int fogenabled;
uniform vec3 camerapos;
varying vec3 pixelpos;

vec4 lightpixel(vec4 pixin)
{
	if (fogenabled != 0)
	{
		const float LOG2E = 1.442692;	// = 1/log(2)
		float fc;
		if (fogenabled == 1) fc = fogcoord;
		else fc = distance(pixelpos, camerapos);
		float factor = exp2 ( -gl_Fog.density * fc * LOG2E);
		pixin *= gl_Color;
		return vec4(mix(gl_Fog.color, pixin, factor).rgb, pixin.a);
	}
	else return pixin * gl_Color;
}
