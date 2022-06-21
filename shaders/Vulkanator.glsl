#extension GL_EXT_shader_explicit_arithmetic_types : require

const uint32_t DEPTH08 =  8u / 16u;
const uint32_t DEPTH16 = 16u / 16u;
const uint32_t DEPTH32 = 32u / 16u;

const uint32_t DEPTH16MAX = 0x8000;
// image data coming in, is [0x0000, 0x8000], but we need [0x0000, 0xFFFF]
const float32_t DEPTH16_LOAD_SCALE = float(0x10000) / DEPTH16MAX;
// image data coming out, should be [0x0000, 0x8000]
const float32_t DEPTH16_STORE_SCALE = DEPTH16MAX / float(0x10000);

struct VulkanatorRenderParams
{
	uint32_t Depth;
	f32mat4 Transform;
	f32vec4 ColorFactor;
};