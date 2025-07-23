// ======================================================================================
// RTWeekend Example Renderer
// --------------------------------------------------------------------------------------
// This is a minimal example of voxel ray tracing using the Cubozoa (cbz)
// rendering API.
//
// Features:
// - Ray tracing implemented via compute shader (`voxel_raytracer.slang`)
// - Camera controls: WASD for horizontal movement, Space/Shift for vertical movement
// - Output written to a structured buffer as RGBA32F, then blitted to screen
// Purpose:
// - Demonstrates compute-based rendering pipeline
// - Serves as a reference for structured buffer usage, compute dispatch, and fullscreen blit
// ======================================================================================

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#define GLM_FORCE_RIGHT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <GLFW/glfw3.h>

#include <cubozoa/cubozoa.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#define GLM_FORCE_RIGHT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <GLFW/glfw3.h>

static std::vector<float> sQuadVertices = {
	// x,   y,     z,   normal,           uv
	-1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, // Vertex 1
	1.0f,  1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, // Vertex 2
	1.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, // Vertex 4
	-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // Vertex 3
};

static std::vector<uint16_t> sQuadIndices = {
	0, 1, 2, // Triangle #0 connects points #0, #1 and #2
	0, 2, 3  // Triangle #1 connects points #0, #2 and #3
};

struct ColorRGBA8 {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

static constexpr uint32_t kWidth = 854;
static constexpr uint32_t kHeight = 480;

constexpr glm::vec3 kVec3Up = { 0.0, 1.0, 0.0 };
constexpr glm::vec3 kVec3Right = { 1.0, 0.0, 0.0 };

class Camera {
public:
	Camera(glm::vec3 startPos) 
		: mLookAt(glm::vec3(0.0f, 0.0f, -1.0f)) {
    mData.position = startPos;
		mData.focalLength = 1.0f;
		mUH = cbz::UniformCreate("uCamera", cbz::UniformType::eVec4, 5);
	}

	~Camera() {
		cbz::UniformDestroy(mUH);
	};

	void lookAt(glm::vec3 lookAt) {
		mLookAt = lookAt;
	}

	void set() {
		mData.forward = glm::normalize(mLookAt - glm::vec3(mData.position));
		mData.right = glm::normalize(glm::cross(mData.forward, kVec3Up));
		mData.up = glm::normalize(glm::cross(mData.right, mData.forward));

		cbz::UniformSet(mUH, mData.cbzUniformVec4());
	}

	// TODO: Encapsulate for reuse
		struct CameraData {
			glm::vec3 position; // Basis right
			uint32_t _padding1;

			glm::vec3 right; // Basis right
			uint32_t _padding2;

			glm::vec3 up; // Basis up 
			uint32_t _padding3;

			glm::vec3 forward; // Basis forward
			uint32_t _padding4;

			float focalLength;
			float focusLength;
			uint32_t _padding5[2];

  		void *cbzUniformVec4() { return reinterpret_cast<void *>(this); }
		} mData;

private:
	glm::vec3 mLookAt;
	cbz::UniformHandle mUH;
};

// DDA Implementation
struct Voxel {
	uint32_t color[3] = { 0, 0, 0 };    // Color of the voxel
	uint32_t isFilled = 0; // Flag indicating if the voxel is empty (0 = empty, 1 = filled) 
};

class VoxelVolume {
public:
	VoxelVolume(uint32_t w, uint32_t h, uint32_t d) {
		mDimensions[0] = w;
		mDimensions[1] = h;
		mDimensions[2] = d;
		mVolumeDescUH = cbz::UniformCreate("uVoxelVolumeDesc", cbz::UniformType::eVec4, 1);

		const size_t voxelCount = static_cast<size_t>(w) * h * d;
		mVoxels.resize(voxelCount);

		const uint32_t quarter = 16 / 4 -1;
		for (uint32_t x = quarter; x < (mDimensions[0] - quarter); x++) {
			for (uint32_t y = quarter; y < (mDimensions[1] - quarter); y++) {
				for (uint32_t z = quarter; z < (mDimensions[2] - quarter); z++) {
					uint32_t voxelIndex = x + y * mDimensions[0] + z * mDimensions[0] * mDimensions[1];

					mVoxels[voxelIndex].color[0] = (uint32_t)(x / (float)mDimensions[0] * 255.0);
					mVoxels[voxelIndex].color[1] = (uint32_t)(y / (float)mDimensions[0] * 255.0);
					mVoxels[voxelIndex].color[2] = (uint32_t)(z / (float)mDimensions[0] * 255.0);

					mVoxels[voxelIndex].isFilled = UINT32_MAX;
				}
			}
		}

		mVolumeSBH = cbz::StructuredBufferCreate(cbz::UniformType::eVec4, voxelCount, mVoxels.data(), "VoxelVolume");
	}

	void set() {
		cbz::UniformSet(mVolumeDescUH, &_cbzUniformVec4);
		cbz::StructuredBufferSet(cbz::BufferSlot::e1, mVolumeSBH);
	}

	~VoxelVolume() {
		cbz::StructuredBufferDestroy(mVolumeSBH);
		cbz::UniformDestroy(mVolumeDescUH);
	}

private:
	cbz::UniformHandle mVolumeDescUH;
	cbz::StructuredBufferHandle mVolumeSBH;

	union {
		union {
			int32_t mDimensions[3]; // (x) width, (y) height, (z) depth
			uint32_t _cbzUniformVec4[4];
		};
	};

	std::vector<Voxel> mVoxels;
};

class Cubozoa {
public:
	Cubozoa(cbz::NetworkStatus netStatus = cbz::NetworkStatus::eClient) {
		if (cbz::Init({ "Cubozoa", kWidth, kHeight, netStatus }) !=
			cbz::Result::eSuccess) {
			exit(0);
		}

		// --- Reset ---
		mLastTime = 0.0f;
		mTime = glfwGetTime();
		mFrameCtr = 0;
		mDeltaTime = 0.0f;

		// --- Common resources ---
		// Create structured buffer with 'eRGBA32Float' color values
		mImageSBH = cbz::StructuredBufferCreate(cbz::UniformType::eVec4,
			kWidth * kHeight, nullptr);

		// --- Blit Pipeline Setup ---
		// Create blit program
		mBlitSH = cbz::ShaderCreate("shaders/blit.spirv");
		mBlitPH = cbz::GraphicsProgramCreate(mBlitSH, "blit_program");

		// Create vertex layout
		cbz::VertexLayout layout = {};
		layout.begin(cbz::VertexStepMode::eVertex);
		layout.push_attribute(cbz::VertexAttributeType::ePosition,
			cbz::VertexFormat::eFloat32x3);
		layout.push_attribute(cbz::VertexAttributeType::eNormal,
			cbz::VertexFormat::eFloat32x3);
		layout.push_attribute(cbz::VertexAttributeType::eTexCoord0,
			cbz::VertexFormat::eFloat32x2);
		layout.end();

		// Create full screen quad vertex and index buffers
		mQuadVBH = cbz::VertexBufferCreate(
			layout, static_cast<uint32_t>(sQuadVertices.size()),
			sQuadVertices.data());

		mQuadIBH = cbz::IndexBufferCreate(
			cbz::IndexFormat::eUint16, static_cast<uint32_t>(sQuadIndices.size()),
			sQuadIndices.data());

		// Create blit texture
		mBlitTH = cbz::Texture2DCreate(cbz::TextureFormat::eRGBA8Unorm, kWidth,
			kHeight, "blitTexture");

		// --- Voxel Ray Tracing Setup ---
		// Create raytracing compute program
		mRaytracingSH = cbz::ShaderCreate("shaders/voxel_raytracing.spirv",
			"raytracing_shader");

		mRaytracingPH = cbz::ComputeProgramCreate(mRaytracingSH);

		// Create ray tracing uniforms
		mRayTracingUH =
			cbz::UniformCreate("uRaytracingSettings", cbz::UniformType::eVec4);

		mVolume = std::make_unique<VoxelVolume>(16, 16, 16);

		mCamera = std::make_unique<Camera>(glm::vec3{1.88, 1.97, 0.68});
		mCamera->lookAt(glm::vec3(8.0, 8.0, 8.0));
	}

	~Cubozoa() {
		// Destroy common resources
		cbz::TextureDestroy(mBlitTH);

		// Destroy compute resources
		cbz::ShaderDestroy(mRaytracingSH);
		cbz::ComputeProgramDestroy(mRaytracingPH);
		cbz::UniformDestroy(mRayTracingUH);
		cbz::StructuredBufferDestroy(mImageSBH);

		// Destroy blit resources
		cbz::VertexBufferDestroy(mQuadVBH);
		cbz::IndexBufferDestroy(mQuadIBH);
		cbz::GraphicsProgramDestroy(mBlitPH);
		cbz::ShaderDestroy(mBlitSH);

		cbz::Shutdown();
		mDeltaTime = 0;
	}

	void update() {
		mLastTime = mTime;
		mTime = glfwGetTime();
		mDeltaTime = mTime - mLastTime;

		struct {
			uint32_t dim[4] = { kWidth, kHeight, 0, 0 };
		} raytracingSettings;
		raytracingSettings.dim[2] = mFrameCtr;

		float movementSpeed = 5;
		if (cbz::IsKeyDown(cbz::Key::eW)) {
			printf("%.2f %.2f %.2f\n", mCamera->mData.position.x, mCamera->mData.position.y, mCamera->mData.position.z);
			mCamera->mData.position += glm::vec3(0.0, 0.0, 1.0) * mDeltaTime * movementSpeed;
		}

		if (cbz::IsKeyDown(cbz::Key::eS)) {
			printf("%.2f %.2f %.2f\n", mCamera->mData.position.x, mCamera->mData.position.y, mCamera->mData.position.z);
			mCamera->mData.position -= glm::vec3(0.0, 0.0, 1.0) * mDeltaTime * movementSpeed;
		}

		if (cbz::IsKeyDown(cbz::Key::eD)) {
			printf("%.2f %.2f %.2f\n", mCamera->mData.position.x, mCamera->mData.position.y, mCamera->mData.position.z);
			//mCamera->mData.position += mCamera->right * mDeltaTime * movementSpeed;
			mCamera->mData.position += glm::vec3(1.0, 0.0, 0.0) * mDeltaTime * movementSpeed;
		}

		if (cbz::IsKeyDown(cbz::Key::eA)) {
			printf("%.2f %.2f %.2f\n", mCamera->mData.position.x, mCamera->mData.position.y, mCamera->mData.position.z);
			//mCamera->position -= mCamera->right * mDeltaTime * movementSpeed;
			mCamera->mData.position -= glm::vec3(1.0, 0.0, 0.0) * mDeltaTime * movementSpeed;
		}

		if (cbz::IsKeyDown(cbz::Key::eSpace)) {
			printf("%.2f %.2f %.2f\n", mCamera->mData.position.x, mCamera->mData.position.y, mCamera->mData.position.z);
			mCamera->mData.position.y += mDeltaTime * movementSpeed;
		}

		if (cbz::IsKeyDown(cbz::Key::eLeftShift)) {
			printf("%.2f %.2f %.2f\n", mCamera->mData.position.x, mCamera->mData.position.y, mCamera->mData.position.z);
			mCamera->mData.position.y -= mDeltaTime * movementSpeed;
		}

		// --- Compute pass ---
		// Set 'mImageSBH' structured buffer to populate
		cbz::StructuredBufferSet(cbz::BufferSlot::e0, mImageSBH, true);

		// Set voxel volume
		mVolume->set();

		// Set camera
		mCamera->set();

		cbz::UniformSet(mRayTracingUH, &raytracingSettings);

		// Submit to compute target
		Submit(0, mRaytracingPH, (kWidth + 7) / 8, (kHeight + 7) / 8, 1);

		// --- Blit pass ---
		// Set populated mImageSBH from compute pass
		cbz::StructuredBufferSet(cbz::BufferSlot::e1, mImageSBH);

		// Set quad vertex and index buffers
		cbz::VertexBufferSet(mQuadVBH);
		cbz::IndexBufferSet(mQuadIBH);

		// Set graphics transform to identity
		glm::mat4 transform = glm::mat4(1.0f);
		transform = glm::transpose(transform);
		cbz::TransformSet(glm::value_ptr(transform));

		// Submit to graphics target
		cbz::Submit(1, mBlitPH);

		cbz::Frame();

		mFrameCtr++;
	}

private:
	std::unique_ptr<Camera> mCamera;
	std::unique_ptr<VoxelVolume> mVolume;

	// Common raytraced image buffer
	cbz::StructuredBufferHandle mImageSBH;

	// Blit resources
	cbz::ShaderHandle mBlitSH;
	cbz::GraphicsProgramHandle mBlitPH;
	cbz::VertexBufferHandle mQuadVBH;
	cbz::IndexBufferHandle mQuadIBH;
	cbz::TextureHandle mBlitTH;

	// Compute resources
	cbz::UniformHandle mRayTracingUH;
	cbz::ShaderHandle mRaytracingSH;
	cbz::ComputeProgramHandle mRaytracingPH;

	// Application resources
	float mTime;
	float mLastTime;
	float mDeltaTime;
	uint32_t mFrameCtr;
};

int main(int argc, char** argv) {
	for (int i = 0; i < argc; i++) {
		printf("%s", argv[i]);
	}

	Cubozoa app(argc > 1 ? cbz::NetworkStatus::eHost : cbz::NetworkStatus::eClient);

#ifndef __EMSCRIPTEN__
	while (true) {
		app.update();
	}
#else
	emscripten_set_main_loop_arg(
		[](void* userData) {
			static int i = 0;
			Cubozoa* app = reinterpret_cast<Cubozoa*>(userData);
			app->update();
		},
		(void*)&app, // value sent to the 'userData' arg of the callback
		0, true);
#endif
}
