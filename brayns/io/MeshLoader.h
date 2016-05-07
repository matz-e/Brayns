/* Copyright (c) 2011-2016, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Cyrille Favreau <cyrille.favreau@epfl.ch>
 *
 * This file is part of BRayns
 */

#ifndef MESHLOADER_H
#define MESHLOADER_H

#include <brayns/common/types.h>
#include <brayns/common/material/Material.h>
#include <brayns/common/material/Texture2D.h>
#include <brayns/common/geometry/TrianglesMesh.h>
#include <brayns/parameters/GeometryParameters.h>

#include <string>

class aiScene;

namespace brayns
{

/** structure containing references to triangles, materials and bounding for
    for all meshes
*/
struct MeshContainer
{
    TrianglesMeshMap& triangles;
    Materials& materials;
    Boxf& bounds;
};

/** Loads meshes from files using the assimp library
 * http://assimp.sourceforge.net
 */
class MeshLoader
{
public:
    MeshLoader();

    /** Imports meshes from a given file
     *
     * @param filename name of the file containing the meshes
     * @param meshContainer structure containing references to triangles and
     *        materials imported from the file, as well as the bounding box of
     *        all meshes.
     * @param meshQuality can be MQ_FAST, MQ_QUALITY or MQ_MAX_QUALITY. Appart
     *        from MQ_FAST, normals are automatically generated is not in the
     *        file.
     * @param material Default material for the whole mesh. If set to
     *        NO_MATERIAL, materials from the mesh file are used. Otherwise,
     *        all meshes are forced to that specific material.
     * @return true if the file was successfully imported. False otherwise.
     */
    bool importMeshFromFile(
            const std::string& filename,
            MeshContainer& meshContainer,
            MeshQuality meshQuality,
            size_t material);

    /** Exports meshes to a given file
     *
     * @param filename destination file name
     * @param meshContainer structure containing references to triangles and
     *        materials imported from the file, as well as the bounding box of
     *        all meshes.
     */
    bool exportMeshToFile(
            const std::string& filename,
            MeshContainer& meshContainer ) const;

private:
    void _createMaterials(
        const aiScene *scene,
        const std::string& folder,
        Materials& materials );

    std::map<size_t, size_t> _meshIndex;
};

}

#endif // MESHLOADER_H