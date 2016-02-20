#ifndef M_H

union vec2 {
	float s[2];
	struct { float x,y; };
	struct { float u,v; };
};

union vec4 {
	float s[4];
	struct { float x,y,z,w; };
	struct { float r,g,b,a; };
};

#define M_H
#endif
