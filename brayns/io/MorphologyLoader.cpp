/* Copyright (c) 2015-2017, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Cyrille Favreau <cyrille.favreau@epfl.ch>
 *
 * This file is part of Brayns <https://github.com/BlueBrain/Brayns>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "MorphologyLoader.h"

#include <brayns/common/geometry/Cone.h>
#include <brayns/common/geometry/Cylinder.h>
#include <brayns/common/geometry/Sphere.h>
#include <brayns/common/log.h>
#include <brayns/common/scene/Scene.h>
#include <brayns/common/simulation/CircuitSimulationHandler.h>
#include <brayns/io/algorithms/MetaballsGenerator.h>

#include <algorithm>
#include <fstream>

#include <boost/filesystem.hpp>

#ifdef BRAYNS_USE_BRION
#include <brain/brain.h>
#include <brion/brion.h>
#endif

namespace brayns
{
MorphologyLoader::MorphologyLoader(const GeometryParameters& geometryParameters)
    : _geometryParameters(geometryParameters)
{
}

#ifdef BRAYNS_USE_BRION
size_t _getMaterialFromSectionType(const size_t morphologyIndex,
                                   const size_t forcedMaterial,
                                   const brain::neuron::SectionType sectionType,
                                   const ColorScheme colorScheme)
{
    if (forcedMaterial != NO_MATERIAL)
        return forcedMaterial;
    size_t material;
    switch (colorScheme)
    {
    case ColorScheme::neuron_by_id:
        material = morphologyIndex % (NB_MAX_MATERIALS - NB_SYSTEM_MATERIALS);
        break;
    case ColorScheme::neuron_by_segment_type:
        size_t s;
        switch (sectionType)
        {
        case brain::neuron::SectionType::soma:
            s = 1;
            break;
        case brain::neuron::SectionType::axon:
            s = 2;
            break;
        case brain::neuron::SectionType::dendrite:
            s = 3;
            break;
        case brain::neuron::SectionType::apicalDendrite:
            s = 4;
            break;
        default:
            s = 0;
            break;
        }
        material = s % (NB_MAX_MATERIALS - NB_SYSTEM_MATERIALS);
        break;
    default:
        material = 0;
    }
    return material;
}

brain::neuron::SectionTypes _getSectionTypes(
    const size_t morphologySectionTypes)
{
    brain::neuron::SectionTypes sectionTypes;
    if (morphologySectionTypes & MST_SOMA)
        sectionTypes.push_back(brain::neuron::SectionType::soma);
    if (morphologySectionTypes & MST_AXON)
        sectionTypes.push_back(brain::neuron::SectionType::axon);
    if (morphologySectionTypes & MST_DENDRITE)
        sectionTypes.push_back(brain::neuron::SectionType::dendrite);
    if (morphologySectionTypes & MST_APICAL_DENDRITE)
        sectionTypes.push_back(brain::neuron::SectionType::apicalDendrite);
    return sectionTypes;
}

bool MorphologyLoader::_importMorphologyAsMesh(
    const servus::URI& source, const size_t morphologyIndex,
    const MaterialsMap& materials, const Matrix4f& transformation,
    TrianglesMeshMap& meshes, Boxf& bounds, const size_t forcedMaterial)
{
    try
    {
        const size_t morphologySectionTypes =
            _geometryParameters.getMorphologySectionTypes();

        brain::neuron::Morphology morphology(source, transformation);
        brain::neuron::SectionTypes sectionTypes =
            _getSectionTypes(morphologySectionTypes);

        const brain::neuron::Sections& sections =
            morphology.getSections(sectionTypes);

        Spheres metaballs;

        if (morphologySectionTypes & MST_SOMA)
        {
            // Soma
            const brain::neuron::Soma& soma = morphology.getSoma();
            const size_t material = _getMaterialFromSectionType(
                morphologyIndex, forcedMaterial,
                brain::neuron::SectionType::soma,
                _geometryParameters.getColorScheme());
            const Vector3f center = soma.getCentroid();

            const float radius =
                (_geometryParameters.getRadiusCorrection() != 0.f
                     ? _geometryParameters.getRadiusCorrection()
                     : soma.getMeanRadius() *
                           _geometryParameters.getRadiusMultiplier());

            metaballs.push_back(
                SpherePtr(new Sphere(material, center, radius, 0.f, 0.f)));
            bounds.merge(center);
        }

        // Dendrites and axon
        for (size_t s = 0; s < sections.size(); ++s)
        {
            const auto& section = sections[s];
            const bool hasParent = section.hasParent();
            if (hasParent)
            {
                const auto parentSectionType = section.getParent().getType();
                if (parentSectionType != brain::neuron::SectionType::soma)
                    continue;
            }

            const auto material = _getMaterialFromSectionType(
                morphologyIndex, forcedMaterial, section.getType(),
                _geometryParameters.getColorScheme());
            const auto& samples = section.getSamples();
            if (samples.empty())
                continue;

            const size_t samplesFromSoma =
                _geometryParameters.getMetaballsSamplesFromSoma();
            const size_t samplesToProcess =
                std::min(samplesFromSoma, samples.size());
            for (size_t i = 0; i < samplesToProcess; ++i)
            {
                const auto& sample = samples[i];
                const Vector3f position(sample.x(), sample.y(), sample.z());
                const auto radius =
                    (_geometryParameters.getRadiusCorrection() != 0.f
                         ? _geometryParameters.getRadiusCorrection()
                         : sample.w() * 0.5f *
                               _geometryParameters.getRadiusMultiplier());

                if (radius > 0.f)
                    metaballs.push_back(SpherePtr(
                        new Sphere(material, position, radius, 0.f, 0.f)));

                bounds.merge(position);
            }
        }

        // Generate mesh
        const size_t gridSize = _geometryParameters.getMetaballsGridSize();
        const float threshold = _geometryParameters.getMetaballsThreshold();
        MetaballsGenerator metaballsGenerator;
        const size_t material =
            _getMaterialFromSectionType(morphologyIndex, forcedMaterial,
                                        brain::neuron::SectionType::soma,
                                        _geometryParameters.getColorScheme());
        metaballsGenerator.generateMesh(metaballs, gridSize, threshold,
                                        materials, material, meshes);
    }
    catch (const std::runtime_error& e)
    {
        BRAYNS_ERROR << e.what() << std::endl;
        return false;
    }
    return true;
}

bool MorphologyLoader::importMorphology(const servus::URI& uri,
                                        const int morphologyIndex, Scene& scene)
{
    bool returnValue = true;
    if (_geometryParameters.useMetaballs())
    {
        returnValue =
            _importMorphologyAsMesh(uri, morphologyIndex, scene.getMaterials(),
                                    Matrix4f(), scene.getTriangleMeshes(),
                                    scene.getWorldBounds());
    }
    float maxDistanceToSoma;
    returnValue = returnValue &&
                  _importMorphology(uri, morphologyIndex, Matrix4f(), nullptr,
                                    scene.getSpheres(), scene.getCylinders(),
                                    scene.getCones(), scene.getWorldBounds(), 0,
                                    maxDistanceToSoma);
    return returnValue;
}

bool MorphologyLoader::_importMorphology(
    const servus::URI& source, const size_t morphologyIndex,
    const Matrix4f& transformation,
    const SimulationInformation* simulationInformation, SpheresMap& spheres,
    CylindersMap& cylinders, ConesMap& cones, Boxf& bounds,
    const size_t simulationOffset, float& maxDistanceToSoma,
    const size_t forcedMaterial)
{
    maxDistanceToSoma = 0.f;
    try
    {
        Vector3f translation = {0.f, 0.f, 0.f};

        brain::neuron::Morphology morphology(source, transformation);
        brain::neuron::SectionTypes sectionTypes;

        const MorphologyLayout& layout =
            _geometryParameters.getMorphologyLayout();

        if (layout.nbColumns != 0)
        {
            Boxf morphologyAABB;
            const auto& points = morphology.getPoints();
            for (const auto& point : points)
                morphologyAABB.merge({point.x(), point.y(), point.z()});

            const Vector3f positionInGrid = {
                -1.f * layout.horizontalSpacing *
                    static_cast<float>(morphologyIndex % layout.nbColumns),
                -1.f * layout.verticalSpacing *
                    static_cast<float>(morphologyIndex / layout.nbColumns),
                0.f};
            translation = positionInGrid - morphologyAABB.getCenter();
        }

        const size_t morphologySectionTypes =
            _geometryParameters.getMorphologySectionTypes();

        sectionTypes = _getSectionTypes(morphologySectionTypes);

        const brain::neuron::Sections& sections =
            morphology.getSections(sectionTypes);

        size_t sectionId = 0;

        float offset = 0.f;
        if (simulationInformation)
            offset = (*simulationInformation->compartmentOffsets)[sectionId];
        else if (simulationOffset != 0)
            offset = simulationOffset;

        if (!_geometryParameters.useMetaballs() &&
            morphologySectionTypes & MST_SOMA)
        {
            // Soma
            const brain::neuron::Soma& soma = morphology.getSoma();
            const size_t material = _getMaterialFromSectionType(
                morphologyIndex, forcedMaterial,
                brain::neuron::SectionType::soma,
                _geometryParameters.getColorScheme());
            const Vector3f somaPosition = soma.getCentroid() + translation;

            const float radius =
                (_geometryParameters.getRadiusCorrection() != 0.f
                     ? _geometryParameters.getRadiusCorrection()
                     : soma.getMeanRadius() *
                           _geometryParameters.getRadiusMultiplier());

            spheres[material].push_back(SpherePtr(
                new Sphere(material, somaPosition, radius, 0.f, offset)));
            bounds.merge(somaPosition);

            if (_geometryParameters.getUseSimulationModel())
            {
                // When using a simulation model, parametric geometries must
                // occupy as much space as possible in the mesh. This code
                // inserts a Cone between the soma and the beginning of each
                // branch.
                const auto& children = soma.getChildren();
                for (const auto& child : children)
                {
                    const auto& samples = child.getSamples();
                    const Vector3f sample = {samples[0].x(), samples[0].y(),
                                             samples[0].z()};
                    cones[material].push_back(ConePtr(
                        new Cone(material, somaPosition, sample, radius,
                                 samples[0].w() * 0.5f *
                                     _geometryParameters.getRadiusMultiplier(),
                                 0.f, offset)));
                    bounds.merge(sample);
                }
            }
        }

        // Dendrites and axon
        for (const auto& section : sections)
        {
            const size_t material = _getMaterialFromSectionType(
                morphologyIndex, forcedMaterial, section.getType(),
                _geometryParameters.getColorScheme());
            const Vector4fs& samples = section.getSamples();
            if (samples.empty())
                continue;

            Vector4f previousSample = samples[0];
            size_t step = 1;
            switch (_geometryParameters.getGeometryQuality())
            {
            case GeometryQuality::low:
                step = samples.size() - 1;
                break;
            case GeometryQuality::medium:
                step = samples.size() / 2;
                step = (step == 0) ? 1 : step;
                break;
            default:
                step = 1;
            }

            const float distanceToSoma = section.getDistanceToSoma();
            const floats& distancesToSoma = section.getSampleDistancesToSoma();

            float segmentStep = 0.f;
            if (simulationInformation)
            {
                const auto& counts = *simulationInformation->compartmentCounts;
                // Number of compartments usually differs from number of samples
                if (samples.empty() && counts[sectionId] > 1)
                    segmentStep = counts[sectionId] / float(samples.size());
            }

            bool done = false;
            for (size_t i = step; !done && i < samples.size() + step; i += step)
            {
                if (i >= samples.size())
                {
                    i = samples.size() - 1;
                    done = true;
                }

                const float distance = distanceToSoma + distancesToSoma[i];

                maxDistanceToSoma = std::max(maxDistanceToSoma, distance);

                if (simulationInformation)
                    offset = (*simulationInformation
                                   ->compartmentOffsets)[sectionId] +
                             float(i) * segmentStep;
                else if (simulationOffset != 0)
                    offset = simulationOffset + distance;

                Vector4f sample = samples[i];
                const float previousRadius =
                    (_geometryParameters.getRadiusCorrection() != 0.f
                         ? _geometryParameters.getRadiusCorrection()
                         : samples[i - step].w() * 0.5f *
                               _geometryParameters.getRadiusMultiplier());

                Vector3f position(sample.x(), sample.y(), sample.z());
                position += translation;
                Vector3f target(previousSample.x(), previousSample.y(),
                                previousSample.z());
                target += translation;
                const float radius =
                    (_geometryParameters.getRadiusCorrection() != 0.f
                         ? _geometryParameters.getRadiusCorrection()
                         : samples[i].w() * 0.5f *
                               _geometryParameters.getRadiusMultiplier());

                if (radius > 0.f)
                    spheres[material].push_back(
                        SpherePtr(new Sphere(material, position, radius,
                                             distance, offset)));

                bounds.merge(position);
                if (position != target && radius > 0.f && previousRadius > 0.f)
                {
                    if (radius == previousRadius)
                        cylinders[material].push_back(CylinderPtr(
                            new Cylinder(material, position, target, radius,
                                         distance, offset)));
                    else
                        cones[material].push_back(ConePtr(
                            new Cone(material, position, target, radius,
                                     previousRadius, distance, offset)));
                    bounds.merge(target);
                }
                previousSample = sample;
            }
            ++sectionId;
        }
    }
    catch (const std::runtime_error& e)
    {
        BRAYNS_ERROR << e.what() << std::endl;
        return false;
    }
    return true;
}

brion::NeuronAttributes getNeuronAttributes(const ColorScheme& colorScheme)
{
    brion::NeuronAttributes neuronAttributes;
    switch (colorScheme)
    {
    case ColorScheme::neuron_by_layer:
        neuronAttributes = brion::NEURON_LAYER;
        break;
    case ColorScheme::neuron_by_mtype:
        neuronAttributes = brion::NEURON_MTYPE;
        break;
    case ColorScheme::neuron_by_etype:
        neuronAttributes = brion::NEURON_ETYPE;
        break;
    default:
        neuronAttributes = brion::NEURON_ALL;
        break;
    }
    return neuronAttributes;
}

bool getNeuronMatrix(const brion::BlueConfig& bc, const brain::GIDSet& gids,
                     const ColorScheme colorScheme, strings& neuronMatrix)
{
    brion::NeuronAttributes neuronAttributes = getNeuronAttributes(colorScheme);
    if (neuronAttributes == brion::NEURON_ALL)
        return false;
    try
    {
        brion::Circuit brionCircuit(bc.getCircuitSource());
        for (const auto& a : brionCircuit.get(gids, neuronAttributes))
            neuronMatrix.push_back(a[0]);
        return true;
    }
    catch (...)
    {
        BRAYNS_WARN << "Only MVD2 format is currently supported by Brion "
                       "circuits. Color scheme by layer, e-type or m-type is "
                       "not available for this circuit"
                    << std::endl;
    }
    return false;
}

bool MorphologyLoader::importCircuit(const servus::URI& circuitConfig,
                                     const std::string& target, Scene& scene,
                                     MeshLoader& meshLoader)
{
    const brion::BlueConfig bc(circuitConfig.getPath());
    const brain::Circuit circuit(bc);
    const auto& gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }
    const auto& transforms = circuit.getTransforms(gids);
    const auto& uris = circuit.getMorphologyURIs(gids);

    BRAYNS_INFO << "Loading " << uris.size() << " cells" << std::endl;

    // Read Brion circuit
    strings neuronMatrix;
    bool mvd3Support =
        getNeuronMatrix(bc, gids, _geometryParameters.getColorScheme(),
                        neuronMatrix);

    std::map<size_t, float> morphologyOffsets;
    size_t simulationOffset = 1;
    size_t simulatedCells = 0;
    size_t progress = 0;
    const auto circuitDensity = _geometryParameters.getCircuitDensity();
    const size_t nbSkippedCells =
        uris.size() / (uris.size() * circuitDensity / 100);

    const auto meshedMorphologiesFolder =
        _geometryParameters.getMeshedMorphologiesFolder();

    bool loadParametricGeometry = true;
    if (!meshedMorphologiesFolder.empty())
    {
        // Loading meshes is currently sequential. TODO: Make it parallel!!!
        for (size_t i = 0; i < uris.size(); ++i)
        {
            if (nbSkippedCells != 0 && i % nbSkippedCells != 0)
            {
                BRAYNS_PROGRESS(progress, uris.size());
                ++progress;
                continue;
            }

            const size_t material =
                mvd3Support
                    ? boost::lexical_cast<size_t>(neuronMatrix[i])
                    : _getMaterialFromSectionType(
                          i, NO_MATERIAL, brain::neuron::SectionType::undefined,
                          _geometryParameters.getColorScheme());

            const auto& uri = uris[i];
            const auto filenameNoExt =
                boost::filesystem::path(uri.getPath()).stem().string();
            std::string meshFilename = meshedMorphologiesFolder + "/" +
                                       filenameNoExt.c_str() +
                                       ".h5.bin_decimated.off";
            meshLoader.importMeshFromFile(
                meshFilename, scene, _geometryParameters.getGeometryQuality(),
                transforms[i], material);
            BRAYNS_PROGRESS(progress, uris.size());
            ++progress;
        }
        loadParametricGeometry = _geometryParameters.getUseSimulationModel();
    }

    if (loadParametricGeometry)
    {
        progress = 0;
#pragma omp parallel
        {
            SpheresMap private_spheres;
            CylindersMap private_cylinders;
            ConesMap private_cones;
            Boxf private_bounds;
#pragma omp for nowait
            for (size_t i = 0; i < uris.size(); ++i)
            {
                if (nbSkippedCells != 0 && i % nbSkippedCells != 0)
                {
                    BRAYNS_PROGRESS(progress, uris.size());
                    ++progress;
                    continue;
                }

                const auto& uri = uris[i];
                float maxDistanceToSoma = 0.f;
                const size_t material =
                    mvd3Support ? boost::lexical_cast<size_t>(neuronMatrix[i])
                                : NO_MATERIAL;

                if (_geometryParameters.useMetaballs())
                {
                    _importMorphologyAsMesh(uri, i, scene.getMaterials(),
                                            transforms[i],
                                            scene.getTriangleMeshes(),
                                            scene.getWorldBounds(), material);
                }

                if (_importMorphology(uri, i, transforms[i], 0, private_spheres,
                                      private_cylinders, private_cones,
                                      private_bounds, simulationOffset,
                                      maxDistanceToSoma, material))
                {
                    morphologyOffsets[simulatedCells] = maxDistanceToSoma;
                    simulationOffset += maxDistanceToSoma;
                }

                BRAYNS_PROGRESS(progress, uris.size());
#pragma omp atomic
                ++progress;
            }

#pragma omp critical
            for (const auto& p : private_spheres)
            {
                const size_t material = p.first;
                scene.getSpheres()[material].insert(
                    scene.getSpheres()[material].end(),
                    private_spheres[material].begin(),
                    private_spheres[material].end());
            }

#pragma omp critical
            for (const auto& p : private_cylinders)
            {
                const size_t material = p.first;
                scene.getCylinders()[material].insert(
                    scene.getCylinders()[material].end(),
                    private_cylinders[material].begin(),
                    private_cylinders[material].end());
            }

#pragma omp critical
            for (const auto& p : private_cones)
            {
                const size_t material = p.first;
                scene.getCones()[material].insert(
                    scene.getCones()[material].end(),
                    private_cones[material].begin(),
                    private_cones[material].end());
            }

            scene.getWorldBounds().merge(private_bounds);
        }
    }
    return true;
}

bool MorphologyLoader::importCircuit(const servus::URI& circuitConfig,
                                     const std::string& target,
                                     const std::string& report, Scene& scene,
                                     MeshLoader& meshLoader)
{
    const std::string& filename = circuitConfig.getPath();
    const brion::BlueConfig bc(filename);
    const brain::Circuit circuit(bc);
    const brain::GIDSet& gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }
    const Matrix4fs& transforms = circuit.getTransforms(gids);

    const brain::URIs& uris = circuit.getMorphologyURIs(gids);

    // Load simulation information from compartment reports
    const brion::CompartmentReport compartmentReport(
        brion::URI(bc.getReportSource(report).getPath()), brion::MODE_READ,
        gids);

    const brion::CompartmentCounts& compartmentCounts =
        compartmentReport.getCompartmentCounts();

    const brion::SectionOffsets& compartmentOffsets =
        compartmentReport.getOffsets();

    brain::URIs cr_uris;
    const brain::GIDSet& cr_gids = compartmentReport.getGIDs();

    // Read Brion circuit
    strings neuronMatrix;
    bool mvd3Support =
        getNeuronMatrix(bc, gids, _geometryParameters.getColorScheme(),
                        neuronMatrix);

    BRAYNS_INFO << "Loading " << cr_gids.size() << " simulated cells"
                << std::endl;
    for (const auto cr_gid : cr_gids)
    {
        auto it = std::find(gids.begin(), gids.end(), cr_gid);
        auto index = std::distance(gids.begin(), it);
        cr_uris.push_back(uris[index]);
    }

    size_t progress = 0;
    const auto circuitDensity = _geometryParameters.getCircuitDensity();
    const size_t nbSkippedCells =
        uris.size() / (uris.size() * circuitDensity / 100);

    const auto meshedMorphologiesFolder =
        _geometryParameters.getMeshedMorphologiesFolder();

    bool loadParametricGeometry = true;
    if (!meshedMorphologiesFolder.empty())
    {
        // Loading meshes is currently sequential. TODO: Make it parallel!!!
        for (size_t i = 0; i < uris.size(); ++i)
        {
            if (nbSkippedCells != 0 && i % nbSkippedCells != 0)
            {
                BRAYNS_PROGRESS(progress, uris.size());
                ++progress;
                continue;
            }

            const size_t material =
                mvd3Support
                    ? boost::lexical_cast<size_t>(neuronMatrix[i])
                    : _getMaterialFromSectionType(
                          i, NO_MATERIAL, brain::neuron::SectionType::undefined,
                          _geometryParameters.getColorScheme());

            const auto& uri = uris[i];
            const auto filenameNoExt =
                boost::filesystem::path(uri.getPath()).stem().string();
            std::string meshFilename = meshedMorphologiesFolder + "/" +
                                       filenameNoExt.c_str() +
                                       ".h5.bin_decimated.off";
            meshLoader.importMeshFromFile(
                meshFilename, scene, _geometryParameters.getGeometryQuality(),
                transforms[i], material);
            BRAYNS_PROGRESS(progress, uris.size());
            ++progress;
        }
        loadParametricGeometry = _geometryParameters.getUseSimulationModel();
    }

    if (loadParametricGeometry)
    {
        progress = 0;
#pragma omp parallel
        {
            SpheresMap private_spheres;
            CylindersMap private_cylinders;
            ConesMap private_cones;
            Boxf private_bounds;
#pragma omp for nowait
            for (size_t i = 0; i < cr_uris.size(); ++i)
            {
                if (nbSkippedCells != 0 && i % nbSkippedCells != 0)
                {
                    BRAYNS_PROGRESS(progress, uris.size());
                    ++progress;
                    continue;
                }

                const auto& uri = cr_uris[i];
                const SimulationInformation simulationInformation = {
                    &compartmentCounts[i], &compartmentOffsets[i]};
                const size_t material =
                    mvd3Support ? boost::lexical_cast<size_t>(neuronMatrix[i])
                                : NO_MATERIAL;

                if (_geometryParameters.useMetaballs())
                {
                    _importMorphologyAsMesh(uri, i, scene.getMaterials(),
                                            transforms[i],
                                            scene.getTriangleMeshes(),
                                            scene.getWorldBounds(), material);
                }

                float maxDistanceToSoma;
                _importMorphology(uri, i, transforms[i], &simulationInformation,
                                  private_spheres, private_cylinders,
                                  private_cones, private_bounds, 0,
                                  maxDistanceToSoma, material);
                BRAYNS_PROGRESS(progress, cr_uris.size());
#pragma omp atomic
                ++progress;
            }

#pragma omp critical
            for (const auto& p : private_spheres)
            {
                const size_t material = p.first;
                scene.getSpheres()[material].insert(
                    scene.getSpheres()[material].end(),
                    private_spheres[material].begin(),
                    private_spheres[material].end());
            }

#pragma omp critical
            for (const auto& p : private_cylinders)
            {
                const size_t material = p.first;
                scene.getCylinders()[material].insert(
                    scene.getCylinders()[material].end(),
                    private_cylinders[material].begin(),
                    private_cylinders[material].end());
            }

#pragma omp critical
            for (const auto& p : private_cones)
            {
                const size_t material = p.first;
                scene.getCones()[material].insert(
                    scene.getCones()[material].end(),
                    private_cones[material].begin(),
                    private_cones[material].end());
            }

            scene.getWorldBounds().merge(private_bounds);
        }
    }

    size_t nonSimulatedCells = _geometryParameters.getNonSimulatedCells();
    if (nonSimulatedCells != 0)
    {
        // Non simulated cells
        const brain::GIDSet& allGids = circuit.getGIDs();
        const brain::URIs& allUris = circuit.getMorphologyURIs(allGids);
        const Matrix4fs& allTransforms = circuit.getTransforms(allGids);

        cr_uris.clear();
        size_t index = 0;
        for (const auto gid : allGids)
        {
            auto it = std::find(cr_gids.begin(), cr_gids.end(), gid);
            if (it == cr_gids.end())
                cr_uris.push_back(allUris[index]);
            ++index;
        }

        if (cr_uris.size() < nonSimulatedCells)
            nonSimulatedCells = cr_uris.size();

        BRAYNS_INFO << "Loading " << nonSimulatedCells << " non-simulated cells"
                    << std::endl;

        progress = 0;
#pragma omp parallel
        {
            SpheresMap private_spheres;
            CylindersMap private_cylinders;
            ConesMap private_cones;
            Boxf private_bounds;
#pragma omp for nowait
            for (size_t i = 0; i < nonSimulatedCells; ++i)
            {
                if (nbSkippedCells != 0 && i % nbSkippedCells != 0)
                {
                    BRAYNS_PROGRESS(progress, uris.size());
                    ++progress;
                    continue;
                }

                float maxDistanceToSoma;
                const auto& uri = allUris[i];
                const size_t material =
                    mvd3Support
                        ? boost::lexical_cast<size_t>(neuronMatrix[i][0])
                        : NO_MATERIAL;

                if (!meshedMorphologiesFolder.empty())
                {
                    const auto filenameNoExt =
                        boost::filesystem::path(uri.getPath()).stem().string();
                    const std::string meshFilename = meshedMorphologiesFolder +
                                                     filenameNoExt.c_str() +
                                                     ".off";
                    meshLoader.importMeshFromFile(
                        meshFilename, scene,
                        _geometryParameters.getGeometryQuality(), transforms[i],
                        material);
                }
                else
                    _importMorphology(uri, i, allTransforms[i], 0,
                                      private_spheres, private_cylinders,
                                      private_cones, private_bounds, 0,
                                      maxDistanceToSoma, material);

                BRAYNS_PROGRESS(progress, allUris.size());
#pragma omp atomic
                ++progress;
            }

#pragma omp critical
            for (const auto& p : private_spheres)
            {
                const size_t material = p.first;
                scene.getSpheres()[material].insert(
                    scene.getSpheres()[material].end(),
                    private_spheres[material].begin(),
                    private_spheres[material].end());
            }

#pragma omp critical
            for (const auto& p : private_cylinders)
            {
                const size_t material = p.first;
                scene.getCylinders()[material].insert(
                    scene.getCylinders()[material].end(),
                    private_cylinders[material].begin(),
                    private_cylinders[material].end());
            }

#pragma omp critical
            for (const auto& p : private_cones)
            {
                const size_t material = p.first;
                scene.getCones()[material].insert(
                    scene.getCones()[material].end(),
                    private_cones[material].begin(),
                    private_cones[material].end());
            }

            scene.getWorldBounds().merge(private_bounds);
        }
    }
    return true;
}

bool MorphologyLoader::importSimulationData(const servus::URI& circuitConfig,
                                            const std::string& target,
                                            const std::string& report,
                                            Scene& scene)
{
    const std::string& filename = circuitConfig.getPath();
    const brion::BlueConfig bc(filename);
    const brain::Circuit circuit(bc);
    const brain::GIDSet& gids =
        (target.empty() ? circuit.getGIDs() : circuit.getGIDs(target));
    if (gids.empty())
    {
        BRAYNS_ERROR << "Circuit does not contain any cells" << std::endl;
        return false;
    }

    // Load simulation information from compartment reports
    brion::CompartmentReport compartmentReport(
        brion::URI(bc.getReportSource(report).getPath()), brion::MODE_READ,
        gids);

    CircuitSimulationHandlerPtr simulationHandler(
        new CircuitSimulationHandler(_geometryParameters));
    scene.setSimulationHandler(simulationHandler);
    const std::string& cacheFile = _geometryParameters.getSimulationCacheFile();
    if (simulationHandler->attachSimulationToCacheFile(cacheFile))
        // Cache already exists, no need to create it.
        return true;

    BRAYNS_INFO << "Cache file does not exist, creating it" << std::endl;
    std::ofstream file(cacheFile, std::ios::out | std::ios::binary);

    if (!file.is_open())
    {
        BRAYNS_ERROR << "Failed to create cache file" << std::endl;
        return false;
    }

    // Load simulation information from compartment reports
    const float start = compartmentReport.getStartTime();
    const float end = compartmentReport.getEndTime();
    const float step = compartmentReport.getTimestep();

    const float firstFrame =
        std::max(start, _geometryParameters.getStartSimulationTime());
    const float lastFrame =
        std::min(end, _geometryParameters.getEndSimulationTime());
    const uint64_t frameSize = compartmentReport.getFrameSize();

    const uint64_t nbFrames = (lastFrame - firstFrame) / step;

    BRAYNS_INFO
        << "Loading values from compartment report and saving them to cache"
        << std::endl;

    // Write header
    simulationHandler->setNbFrames(nbFrames);
    simulationHandler->setFrameSize(frameSize);
    simulationHandler->writeHeader(file);

    // Write body
    for (uint64_t frame = 0; frame < nbFrames; ++frame)
    {
        BRAYNS_PROGRESS(frame, nbFrames);
        const float frameTime = firstFrame + step * frame;
        const brion::floatsPtr& valuesPtr =
            compartmentReport.loadFrame(frameTime);
        const floats& values = *valuesPtr;
        simulationHandler->writeFrame(file, values);
    }
    file.close();

    BRAYNS_INFO << "----------------------------------------" << std::endl;
    BRAYNS_INFO << "Cache file successfully created" << std::endl;
    BRAYNS_INFO << "Number of frames: " << nbFrames << std::endl;
    BRAYNS_INFO << "Frame size      : " << frameSize << std::endl;
    BRAYNS_INFO << "----------------------------------------" << std::endl;
    return true;
}

#else

bool MorphologyLoader::importMorphology(const servus::URI&, const int, Scene&)
{
    BRAYNS_ERROR << "Brion is required to load morphologies" << std::endl;
    return false;
}

bool MorphologyLoader::importCircuit(const servus::URI&, const std::string&,
                                     Scene&)
{
    BRAYNS_ERROR << "Brion is required to load circuits" << std::endl;
    return false;
}

bool MorphologyLoader::importCircuit(const servus::URI&, const std::string&,
                                     const std::string&, Scene&)
{
    BRAYNS_ERROR << "Brion is required to load circuits" << std::endl;
    return false;
}

bool MorphologyLoader::importSimulationData(const servus::URI&,
                                            const std::string&,
                                            const std::string&, Scene&)
{
    BRAYNS_ERROR << "Brion is required to load circuits" << std::endl;
    return false;
}

#endif
}
