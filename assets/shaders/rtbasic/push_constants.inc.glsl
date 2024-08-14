layout(scalar, push_constant) uniform Constants {
    GlobalBuffer globals;
    MeshDataBuffer mesh_datas;
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    DDGIBuffer ddgi;
    PerTlasInstanceBlasGeometryOffsetBuffer tlas_mesh_offsets;
    PerBlasGeometryTriangleOffsetBuffer blas_mesh_offsets;
    PerTriangleMeshIdBuffer triangle_mesh_ids;
    uint32_t mode;
};
