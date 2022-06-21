#version 460

layout(location = 0) in vec2 InPosition;
layout(location = 1) in vec2 InCoord;

layout(location = 0) out vec2 OutCoord;

layout(binding = 0) uniform Uniforms{
    uint Depth;
    mat4 Transform;
    vec4 ColorFactor;
};

void main()
{
    gl_Position = vec4(
        (Transform * vec4(InPosition, 0.0, 1.0)).xy,
        0.0,
        1.0
    );
    OutCoord = InCoord;
}