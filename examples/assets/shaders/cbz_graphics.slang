import cbz;

namespace cbz {

public struct TransformData {
  float4x4 model;
  float4x4 view;
  float4x4 proj;

  float4x4 model_inv;
  float4x4 view_inv;
  float4x4 proj_inv;
};

[[vk_binding(GLOBAL_TRANSFORM_BUFFER)]]
StructuredBuffer<TransformData> gTransforms;

public struct Draw {
  public float4x4 mvp() { return mul(proj(), mul(view(), model())); }

  public float4x4 viewProj() { return mul(proj(), view()); }

  public float4x4 model() { return gTransforms[_cbzDrawID].model; }

  public float4x4 view() { return gTransforms[_cbzDrawID].view; }

  public float4x4 proj() { return gTransforms[_cbzDrawID].proj; }

  private uint _cbzDrawID : SV_InstanceID;
}

} // namespace cbz