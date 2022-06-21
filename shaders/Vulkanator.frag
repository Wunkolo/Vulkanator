#version 460

layout(location = 0) in vec2 InCoord;

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform Uniforms{
    uint Depth;
    mat4 Transform;
    vec4 ColorFactor;
};

layout(binding = 1) uniform sampler2D InputTexture;

void main() {
    // After effects textures are stored in ARGB format,
    // so we have to "unswizzle" It when we read (argb -> rgba)
    FragColor = texture(InputTexture, InCoord).gbar;

    FragColor *= ColorFactor;

    // After effects stores things in ARGB order (/_\)
    FragColor = FragColor.argb;
}