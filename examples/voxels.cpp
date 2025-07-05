// static std::array<TextureHandle, MAX_DRAW_TEXTURES * MAX_DRAW_CALLS>
// sTextures;

// ---------- Voxel ray tracer ----------
// #include <glm/glm.hpp>
//
// struct Voxel {
//   glm::vec4 normal;
//   glm::vec4 uv;
//
//   // TODO: Make material ptrs
//   glm::vec4 abledo;
//   glm::vec4 emissive;
// };
//
// class PointLight {};
//
// class DirectionalLight {};
//
// class IVoxelMap {};
//
// class DynamicVoxelMap : public IVoxelMap {
// public:
//   DynamicVoxelMap(glm::ivec3 dimensions) : mDimensions(dimensions) {
//     mVoxels.resize(mDimensions.x * mDimensions.y * mDimensions.z);
//     sLogger->trace("DynamicVoxelMap reserving: {} bytes",
//                    mVoxels.size() * sizeof(Voxel));
//   }
//
//   Result insert(glm::ivec3 position, const Voxel &voxel) {
//     mVoxels[getIndex(position)] = voxel;
//     return Result::eSuccess;
//   }
//
//   void clear(glm::ivec3 position) { mVoxels[getIndex(position)] = {}; }
//
//   // void clearRange(glm::ivec3 from, glm::ivec3 to) {
//   // mVoxels[getIndex(position)] = {};
//   // }
//
//   void draw(double dt) {
//
//     // submit(mComputePH);
//
//     vertexBufferBind(mQuadVBH);
//     submit(0, mDyanmicVoxelPH);
//   }
//
//   inline const glm::ivec3 getDimensions() const { return mDimensions; }
//
// private:
//   inline uint32_t getIndex(glm::ivec3 position) const {
//     return position.x +
//            mDimensions.x * (position.y + mDimensions.y * position.z);
//   }
//
// private:
//   ComputeProgramHandle mComputePH;
//   // TextureHandle th;
//
//   ShaderHandle mDynamicVoxelSH;
//   VertexBufferHandle mQuadVBH;
//   GraphicsProgramHandle mDyanmicVoxelPH;
//
//   const glm::ivec3 mDimensions;
//   std::vector<Voxel> mVoxels;
// };
//
// class VoxelMapSVT : public IVoxelMap {
//   // Size: 12 bytes, alignment 1 byte
//   struct [[gnu::packed]] SvtNode64 {
//     uint32_t isLeaf : 1;
//     uint32_t childPtr : 31;
//     uint64_t childMask;
//   };
// };
//
// // TODO: make mutations queued.
// // TODO: Ammortize voxel insertions/mutations.
// // TODO: Voxel world containting multiple voxel maps.
//
// ---------- Voxel ray tracer ----------
