#pragma once

#include <mitsuba/core/ddistr.h>
#include <mitsuba/core/struct.h>
#include <mitsuba/core/transform.h>
#include <mitsuba/render/interaction.h>
#include <mitsuba/render/shape.h>
#include <tbb/tbb.h>

NAMESPACE_BEGIN(mitsuba)

template <typename Float, typename Spectrum>
class MTS_EXPORT_RENDER Mesh : public Shape<Float, Spectrum> {
public:
    MTS_DECLARE_CLASS_VARIANT(Mesh, Shape)
    MTS_IMPORT_TYPES()
    MTS_IMPORT_BASE(Shape, m_mesh)

    // TODO this should depend on the file format
    using InputFloat = float;
    using InputPoint3f  = Point<InputFloat, 3>;
    using InputVector2f = Vector<InputFloat, 2>;
    using InputVector3f = Vector<InputFloat, 3>;
    using InputNormal3f = Normal<InputFloat, 3>;

    using typename Base::ScalarSize;
    using typename Base::ScalarIndex;

    using FaceHolder   = std::unique_ptr<uint8_t[]>;
    using VertexHolder = std::unique_ptr<uint8_t[]>;

    /// Create a new mesh with the given vertex and face data structures
    Mesh(const std::string &name,
         Struct *vertex_struct, ScalarSize vertex_count,
         Struct *face_struct,   ScalarSize face_count);

    // =========================================================================
    //! @{ \name Accessors (vertices, faces, normals, etc)
    // =========================================================================

    /// Return the total number of vertices
    ScalarSize vertex_count() const { return m_vertex_count; }
    /// Return the total number of faces
    ScalarSize face_count() const { return m_face_count; }

    /// Return a \c Struct instance describing the contents of the vertex buffer
    const Struct *vertex_struct() const { return m_vertex_struct.get(); }
    /// Return a \c Struct instance describing the contents of the face buffer
    const Struct *face_struct() const { return m_face_struct.get(); }

    /// Return a pointer to the raw vertex buffer
    uint8_t *vertices() { return m_vertices.get(); }
    /// Const variant of \ref vertices.
    const uint8_t *vertices() const { return m_vertices.get(); }
    /// Const variant of \ref faces.
    uint8_t *faces() { return (uint8_t *) m_faces.get(); }
    /// Return a pointer to the raw face buffer
    const uint8_t *faces() const { return m_faces.get(); }

    /// Return a pointer (or packet of pointers) to a specific vertex
    template <typename Index, typename VertexPtr = replace_scalar_t<Index, uint8_t *>>
    MTS_INLINE VertexPtr vertex(const Index &index) {
        return VertexPtr(m_vertices.get()) + m_vertex_size * index;
    }

    /// Return a pointer (or packet of pointers) to a specific vertex (const version)
    template <typename Index, typename VertexPtr = replace_scalar_t<Index, const uint8_t *>>
    MTS_INLINE VertexPtr vertex(const Index &index) const {
        return VertexPtr(m_vertices.get()) + m_vertex_size * index;
    }

    /// Return a pointer (or packet of pointers) to a specific face
    template <typename Index, typename FacePtr = replace_scalar_t<Index, uint8_t *>>
    MTS_INLINE FacePtr face(const Index &index) {
        return FacePtr(m_faces.get()) + m_face_size * index;
    }

    /// Return a pointer (or packet of pointers) to a specific face (const version)
    template <typename Index, typename FacePtr = replace_scalar_t<Index, const uint8_t *>>
    MTS_INLINE FacePtr face(const Index &index) const {
        return FacePtr(m_faces.get()) + m_face_size * index;
    }

    /// Returns the face indices associated with triangle \c index
    template <typename Index>
    MTS_INLINE auto face_indices(Index index, const mask_t<Index> &active = true) const {
        using Index3 = Array<Index, 3>;
        using Result = uint32_array_t<Index3>;
        if constexpr (!is_array_v<Index>) {
            return load<Result>(face(index));
        } else if constexpr (!is_cuda_array_v<Index>) {
            index *= scalar_t<Index>(m_face_size / sizeof(ScalarIndex));
            return gather<Result, sizeof(ScalarIndex)>(
                m_faces.get(), Index3(index, index + 1u, index + 2u), active);
        }
#if defined(MTS_ENABLE_OPTIX)
        else {
            return gather<Result, sizeof(ScalarIndex)>(m_faces_c, index, active);
        }
#endif
    }

    /// Returns the world-space position of the vertex with index \c index
    template <typename Index>
    MTS_INLINE auto vertex_position(Index index, const mask_t<Index> &active = true) const {
        using Index3 = Array<Index, 3>;
        using Result = Point<replace_scalar_t<Index, ScalarFloat>, 3>;
        if constexpr (!is_array_v<Index>) {
            return load<Result>(vertex(index));
        } else if constexpr (!is_cuda_array_v<Index>) {
            index *= scalar_t<Index>(m_vertex_size / sizeof(ScalarFloat));
            return gather<Result, sizeof(ScalarFloat)>(
                m_vertices.get(), Index3(index, index + 1u, index + 2u), active);
        }
#if defined(MTS_ENABLE_OPTIX)
        else {
            return gather<Result, sizeof(ScalarFloat)>(m_vertex_positions_c, index, active);
        }
#endif
    }

    /// Returns the normal direction of the vertex with index \c index
    template <typename Index>
    MTS_INLINE auto vertex_normal(Index index, const mask_t<Index> &active = true) const {
        using Index3 = Array<Index, 3>;
        using Result = Normal<replace_scalar_t<Index, ScalarFloat>, 3>;
        if constexpr (!is_array_v<Index>) {
            return load_unaligned<Result>(vertex(index) + m_normal_offset);
        } else if constexpr (!is_cuda_array_v<Index>) {
            index *= scalar_t<Index>(m_vertex_size / sizeof(ScalarFloat));
            return gather<Result, sizeof(ScalarFloat)>(
                m_vertices.get() + m_normal_offset, Index3(index, index + 1u, index + 2u), active);
        }
#if defined(MTS_ENABLE_OPTIX)
        else {
            return gather<Result, sizeof(ScalarFloat)>(m_vertex_normals_c, index, active);
        }
#endif
    }

    /// Returns the UV texture coordinates of the vertex with index \c index
    template <typename Index>
    MTS_INLINE auto vertex_texcoord(Index index, const mask_t<Index> &active = true) const {
        using Result = Point<replace_scalar_t<Index, ScalarFloat>, 2>;
        if constexpr (!is_array_v<Index>) {
            return load_unaligned<Result>(vertex(index) + m_texcoord_offset);
        } else if constexpr (!is_cuda_array_v<Index>) {
            index *= scalar_t<Index>(m_vertex_size / sizeof(ScalarFloat));
            return gather<Result, sizeof(ScalarFloat)>(
                m_vertices.get() + m_texcoord_offset, Array<Index, 2>(index, index + 1u), active);
        }
#if defined(MTS_ENABLE_OPTIX)
        else {
            return gather<Result, sizeof(ScalarFloat)>(m_vertex_texcoords_c, index, active);
        }
#endif
    }

    /// Returns the surface area of the face with index \c index
    template <typename Index>
    auto face_area(Index index, mask_t<Index> active = true) const {
        auto fi = face_indices(index, active);

        auto p0 = vertex_position(fi[0], active),
             p1 = vertex_position(fi[1], active),
             p2 = vertex_position(fi[2], active);

        return .5f * norm(cross(p1 - p0, p2 - p0));
    }

    /// Does this mesh have per-vertex normals?
    bool has_vertex_normals() const { return m_normal_offset != 0; }

    /// Does this mesh have per-vertex texture coordinates?
    bool has_vertex_texcoords() const { return m_texcoord_offset != 0; }

    /// Does this mesh have per-vertex texture colors?
    bool has_vertex_colors() const { return m_color_offset != 0; }

    /// @}
    // =========================================================================

    /// Export mesh using the file format implemented by the subclass
    virtual void write(Stream *stream) const;

    /// Compute smooth vertex normals and replace the current normal values
    void recompute_vertex_normals();

    /// Recompute the bounding box (e.g. after modifying the vertex positions)
    void recompute_bbox();

    // =============================================================
    //! @{ \name Shape interface implementation
    // =============================================================

    virtual ScalarBoundingBox3f bbox() const override;

    virtual ScalarBoundingBox3f bbox(ScalarIndex index) const override;

    virtual ScalarBoundingBox3f bbox(ScalarIndex index, const ScalarBoundingBox3f &clip) const override;

    virtual ScalarSize primitive_count() const override;

    virtual ScalarFloat surface_area() const override;

    virtual PositionSample3f sample_position(Float time, const Point2f &sample,
                                             Mask active = true) const override;

    virtual Float pdf_position(const PositionSample3f &ps, Mask active = true) const override;

    virtual void fill_surface_interaction(const Ray3f &ray,
                                          const Float *cache,
                                          SurfaceInteraction3f &si,
                                          Mask active = true) const override;

    virtual std::pair<Vector3f, Vector3f>
    normal_derivative(const SurfaceInteraction3f &si,
                      bool shading_frame = true, Mask active = true) const override;

    /** \brief Ray-triangle intersection test
     *
     * Uses the algorithm by Moeller and Trumbore discussed at
     * <tt>http://www.acm.org/jgt/papers/MollerTrumbore97/code.html</tt>.
     *
     * \param index
     *    Index of the triangle to be intersected.
     * \param ray
     *    The ray segment to be used for the intersection query.
     * \return
     *    Returns an ordered tuple <tt>(mask, u, v, t)</tt>, where \c mask
     *    indicates whether an intersection was found, \c t contains the
     *    distance from the ray origin to the intersection point, and \c u and
     *    \c v contains the first two components of the intersection in
     *    barycentric coordinates
     */
    MTS_INLINE std::tuple<Mask, Float, Float, Float>
    ray_intersect_triangle(const ScalarIndex &index, const Ray3f &ray,
                           identity_t<Mask> active = true) const {
        auto fi = face_indices(index);

        Point3f p0 = vertex_position(fi[0]),
                p1 = vertex_position(fi[1]),
                p2 = vertex_position(fi[2]);

        Vector3f e1 = p1 - p0, e2 = p2 - p0;

        Vector3f pvec = cross(ray.d, e2);
        Float inv_det = rcp(dot(e1, pvec));

        Vector3f tvec = ray.o - p0;
        Float u = dot(tvec, pvec) * inv_det;
        active &= u >= 0.f && u <= 1.f;

        Vector3f qvec = cross(tvec, e1);
        Float v = dot(ray.d, qvec) * inv_det;
        active &= v >= 0.f && u + v <= 1.f;

        Float t = dot(e2, qvec) * inv_det;
        active &= t >= ray.mint && t <= ray.maxt;

        return { active, u, v, t };
    }

#if defined(MTS_USE_EMBREE)
    /// Return the Embree version of this shape
    virtual RTCGeometry embree_geometry(RTCDevice device) const override;
#endif

    void traverse(TraversalCallback *callback) override;

    void parameters_changed() override;

#if defined(MTS_ENABLE_OPTIX)
    /// Return the OptiX version of this shape
    virtual RTgeometrytriangles optix_geometry(RTcontext context) override;
#endif

    /// @}
    // =========================================================================


    /// Return a human-readable string representation of the shape contents.
    virtual std::string to_string() const override;

protected:
    Mesh(const Properties &);
    inline Mesh() { m_mesh = true; }
    virtual ~Mesh();

    /**
     * \brief Prepare internal tables for sampling uniformly wrt. area.
     *
     * Computes the surface area and sets up \ref m_area_distribution.
     * Thread-safe, since it uses a mutex.
     */
    void prepare_sampling_table();

    /**
     * Ensures that the sampling table is ready. This function is not really
     * const, but only makes (thread-safe) writes to \ref m_area_distribution,
     * \ref m_surface_area, and \ref m_inv_surface_area.
     */
    ENOKI_INLINE void ensure_table_ready() const {
        if (unlikely(m_surface_area < 0))
            const_cast<Mesh *>(this)->prepare_sampling_table();
    }

protected:
    VertexHolder m_vertices;
    FaceHolder m_faces;
    ScalarSize m_vertex_size = 0;
    ScalarSize m_face_size = 0;

    /// Byte offset of the normal data within the vertex buffer
    ScalarIndex m_normal_offset = 0;
    /// Byte offset of the texture coordinate data within the vertex buffer
    ScalarIndex m_texcoord_offset = 0;
    /// Byte offset of the color data within the vertex buffer
    ScalarIndex m_color_offset = 0;

    std::string m_name;
    ScalarBoundingBox3f m_bbox;
    ScalarTransform4f m_to_world;

    ScalarSize m_vertex_count = 0;
    ScalarSize m_face_count = 0;

    ref<Struct> m_vertex_struct;
    ref<Struct> m_face_struct;

    /// Flag that can be set by the user to disable loading/computation of vertex normals
    bool m_disable_vertex_normals = false;

#if defined(MTS_ENABLE_OPTIX)
    /* GPU versions of the above */
    Point3u  m_faces_c;
    Point3f  m_vertex_positions_c;
    Normal3f m_vertex_normals_c;
    Point2f  m_vertex_texcoords_c;

    RTcontext m_optix_context = nullptr;
    RTgeometrytriangles m_optix_geometry = nullptr;
    RTbuffer m_optix_faces_buf = nullptr;
    RTbuffer m_optix_vertex_positions_buf = nullptr;
    RTbuffer m_optix_vertex_normals_buf = nullptr;
    RTbuffer m_optix_vertex_texcoords_buf = nullptr;

    bool m_optix_geometry_ready = false;
#endif

    /**
     * Surface area distribution -- generated on demand when \ref
     * prepare_sampling_table() is first called. The value of \ref
     * m_surface_area is negative until this has taken place.
     */
    DiscreteDistribution m_area_distribution;
    ScalarFloat m_surface_area = -1.f;
    ScalarFloat m_inv_surface_area = -1.f;
    tbb::spin_mutex m_mutex;
};

MTS_EXTERN_CLASS(Mesh)
NAMESPACE_END(mitsuba)

// -----------------------------------------------------------------------
//! @{ \name Enoki accessors for dynamic vectorization
// -----------------------------------------------------------------------

// Enable usage of array pointers for our types
ENOKI_CALL_SUPPORT_TEMPLATE_BEGIN(mitsuba::Mesh)
    ENOKI_CALL_SUPPORT_METHOD(fill_surface_interaction)
    ENOKI_CALL_SUPPORT_GETTER_TYPE(faces, m_faces, uint8_t*)
    ENOKI_CALL_SUPPORT_GETTER_TYPE(vertices, m_vertices, uint8_t*)

    // ENOKI_CALL_SUPPORT_METHOD(face)
    // ENOKI_CALL_SUPPORT_METHOD(vertex)

    // ENOKI_CALL_SUPPORT_METHOD(has_vertex_normals)
    // ENOKI_CALL_SUPPORT_METHOD(vertex_position)
    // ENOKI_CALL_SUPPORT_METHOD(vertex_normal)
ENOKI_CALL_SUPPORT_TEMPLATE_END(mitsuba::Mesh)

//! @}
// -----------------------------------------------------------------------
