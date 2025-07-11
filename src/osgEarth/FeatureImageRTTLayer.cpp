/* osgEarth
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include "FeatureImageRTTLayer"
#include <osgEarth/Map>
#include <osgEarth/TileRasterizer>
#include <osgEarth/FilterContext>
#include <osgEarth/GeometryCompiler>
#include <osgEarth/FeatureStyleSorter>
#include <osgEarth/RenderSymbol>

using namespace osgEarth;

#define LC "[FeatureImageRTTLayer] "


REGISTER_OSGEARTH_LAYER(featureimagertt, FeatureImageRTTLayer);
REGISTER_OSGEARTH_LAYER(feature_image_rtt, FeatureImageRTTLayer);

//........................................................................

Config
FeatureImageRTTLayer::Options::getConfig() const
{
    auto conf = super::getConfig();
    featureSource().set(conf, "features");
    styleSheet().set(conf, "styles");
    conf.set("buffer_width", featureBufferWidth());
    conf.set("num_jobs_per_frame", numJobsPerFrame());
    conf.set("num_renderers", numRenderers());

    if (filters().empty() == false)
    {
        Config temp;
        for (unsigned i = 0; i < filters().size(); ++i)
            temp.add(filters()[i].getConfig());
        conf.set("filters", temp);
    }
    return conf;
}

void
FeatureImageRTTLayer::Options::fromConfig(const Config& conf)
{
    numJobsPerFrame().setDefault(4);
    numRenderers().setDefault(4);

    featureSource().get(conf, "features");
    styleSheet().get(conf, "styles");
    conf.get("buffer_width", featureBufferWidth());
    conf.get("num_jobs_per_frame", numJobsPerFrame());
    conf.get("num_renderers", numRenderers());

    const Config& filtersConf = conf.child("filters");
    for (ConfigSet::const_iterator i = filtersConf.children().begin(); i != filtersConf.children().end(); ++i)
        filters().push_back(ConfigOptions(*i));
}

//........................................................................

void
FeatureImageRTTLayer::setFeatureBufferWidth(const Distance& value) {
    options().featureBufferWidth() = value;
}

const Distance&
FeatureImageRTTLayer::getFeatureBufferWidth() const {
    return options().featureBufferWidth().get();
}

void
FeatureImageRTTLayer::setNumJobsPerFrame(unsigned int value)
{
    options().numJobsPerFrame() = value;
    if (_rasterizer.valid())
    {
        _rasterizer->setNumJobsToDispatchPerFrame(value);
    }
}

unsigned int
FeatureImageRTTLayer::getNumJobsPerFrame() const
{
    return options().numJobsPerFrame().get();
}

void
FeatureImageRTTLayer::setNumRenderers(unsigned int value)
{
    options().numRenderers() = value;
    if (_rasterizer.valid())
    {
        _rasterizer->setNumRenderersPerGraphicsContext(value);
    }
}

unsigned int
FeatureImageRTTLayer::getNumRenderers() const
{
    return options().numRenderers().get();
}

void
FeatureImageRTTLayer::init()
{
    super::init();

    // Generate Mercator tiles by default.
    setProfile(Profile::create(Profile::GLOBAL_GEODETIC));

    if (getName().empty())
        setName("FeatureImageRTT");
}

Status
FeatureImageRTTLayer::openImplementation()
{
    Status parent = super::openImplementation();
    if (parent.isError())
        return parent;

    // assert a feature source:
    Status fsStatus = options().featureSource().open(getReadOptions());
    if (fsStatus.isError())
        return fsStatus;

    Status ssStatus = options().styleSheet().open(getReadOptions());
    if (ssStatus.isError())
        return ssStatus;

    // Create a rasterizer for rendering nodes to images.
    if (_rasterizer.valid() == false)
    {
        _rasterizer = new TileRasterizer(getTileSize(), getTileSize());
        _rasterizer->setNumJobsToDispatchPerFrame(getNumJobsPerFrame());
        _rasterizer->setNumRenderersPerGraphicsContext(getNumRenderers());
    }

    _filterChain = FeatureFilterChain::create(
        options().filters(),
        getReadOptions());

    DataExtent de(getProfile()->getExtent(), getMinLevel(), getMaxLevel());
    setDataExtents({ de });

    return Status::NoError;
}

Status
FeatureImageRTTLayer::closeImplementation()
{
    _rasterizer = nullptr;
    return super::closeImplementation();
}

void
FeatureImageRTTLayer::addedToMap(const Map* map)
{
    super::addedToMap(map);

    options().featureSource().addedToMap(map);
    options().styleSheet().addedToMap(map);

    // create a session for feature processing based in the Map,
    // but don't set the feature source yet.
    _session = new Session(map, getStyleSheet(), nullptr, getReadOptions());
    _session->setResourceCache(new ResourceCache());
    _session->setFeatureSource(options().featureSource().getLayer());
}

void
FeatureImageRTTLayer::removedFromMap(const Map* map)
{
    super::removedFromMap(map);
    options().featureSource().removedFromMap(map);
    options().styleSheet().removedFromMap(map);
    _session = nullptr;
}

osg::Node*
FeatureImageRTTLayer::getNode() const
{
    return _rasterizer.get();
}

void
FeatureImageRTTLayer::setFeatureSource(FeatureSource* layer)
{
    if (getFeatureSource() != layer)
    {
        options().featureSource().setLayer(layer);
        if (layer && layer->getStatus().isError())
        {
            setStatus(layer->getStatus());
        }
    }
}

FeatureSource*
FeatureImageRTTLayer::getFeatureSource() const
{
    return options().featureSource().getLayer();
}

void
FeatureImageRTTLayer::setStyleSheet(StyleSheet* value)
{
    options().styleSheet().setLayer(value);
}

StyleSheet*
FeatureImageRTTLayer::getStyleSheet() const
{
    return options().styleSheet().getLayer();
}

GeoImage
FeatureImageRTTLayer::createImageImplementation(const TileKey& key, ProgressCallback* progress) const
{
    if (getStatus().isError())
    {
        return GeoImage(getStatus());
    }

    // take local refs to isolate this method from the member objects
    osg::ref_ptr<FeatureSource> featureSource(getFeatureSource());
    osg::ref_ptr<StyleSheet> styleSheet(getStyleSheet());
    osg::ref_ptr<TileRasterizer> rasterizer(_rasterizer);
    osg::ref_ptr<Session> session(_session);

    if (!featureSource.valid())
    {
        setStatus(Status(Status::ServiceUnavailable, "No feature source"));
        return GeoImage(getStatus());
    }

    if (featureSource->getStatus().isError())
    {
        setStatus(featureSource->getStatus());
        return GeoImage(getStatus());
    }

    osg::ref_ptr<const FeatureProfile> featureProfile = featureSource->getFeatureProfile();
    if (!featureProfile.valid())
    {
        setStatus(Status(Status::ConfigurationError, "Feature profile is missing"));
        return GeoImage(getStatus());
    }

    if (!rasterizer.valid() || !session.valid())
    {
        return GeoImage::INVALID;
    }

    const SpatialReference* featureSRS = featureProfile->getSRS();
    if (!featureSRS)
    {
        setStatus(Status(Status::ConfigurationError, "Feature profile has no SRS"));
        return GeoImage(getStatus());
    }

    GeoExtent featureExtent = key.getExtent().transform(featureSRS);

    // Create the output extent:
    GeoExtent outputExtent = key.getExtent();

    // Establish a local tangent plane near the output extent. This will allow
    // the compiler to render the tile in a local cartesian space.
    const SpatialReference* keySRS = outputExtent.getSRS();
    osg::Vec3d pos(outputExtent.west(), outputExtent.south(), 0);
    osg::ref_ptr<const SpatialReference> local_srs = keySRS->createTangentPlaneSRS(pos);
    outputExtent = outputExtent.transform(local_srs.get());

    // Set the LTP as our output SRS.
    // The geometry compiler will transform all our features into the
    // LTP so we can render using an orthographic camera (TileRasterizer)
    FilterContext fc(session.get(), featureProfile.get(), featureExtent);
    fc.setOutputSRS(outputExtent.getSRS());

    osg::ref_ptr<osg::Group> group;
    int render_order = 0;
    RenderSymbol ordering;

    auto processStyle = [&](const Style& style, auto& features, auto* progress)
        {
            if (!group.valid())
                group = new osg::Group();

            // we can skip geometry optimization since we're rasterizing it:
            GeometryCompiler compiler;
            compiler.options().optimizeVertexOrdering() = false;
            compiler.options().mergeGeometry() = false;
            compiler.options().buildKDTrees() = false;

            osg::ref_ptr<osg::Node> node = compiler.compile(features, style, fc);
            
            if (node.valid() && node->getBound().valid())
            {
                group->addChild(node);

                bool apply_default_ordering = true;
                auto* render = style.get<RenderSymbol>();
                if (render)
                {
                    render->applyTo(node.get());
                    if (render->order().isSet())
                        apply_default_ordering = false;
                }

                // if there's no render-order symbol, enforce rendering in order of appearance.
                if (apply_default_ordering)
                {
                    ordering.order() = ++render_order;
                    ordering.applyTo(node.get());
                }
            }
        };

    FeatureStyleSorter().sort(
        key,
        options().featureBufferWidth().value(),
        session.get(),
        _filterChain,
        nullptr,
        processStyle,
        progress);



    if (group && group->getBound().valid())
    {
        // Make sure there's actually geometry to render in the output extent
        // since rasterization is expensive!
        osg::Polytope polytope;
        outputExtent.createPolytope(polytope);

        osg::ref_ptr<osgUtil::PolytopeIntersector> intersector = new osgUtil::PolytopeIntersector(polytope);
        intersector->setIntersectionLimit(osgUtil::Intersector::LIMIT_ONE);

        osgUtil::IntersectionVisitor visitor(intersector);
        group->accept(visitor);

        if (intersector->getIntersections().empty())
        {
            //OE_DEBUG << LC << "RSL: skipped an EMPTY bounds without rasterizing :) for " << key.str() << std::endl;
            return GeoImage::INVALID;
        }

        group->setName(key.str());

        // rasterize the group in the background:
        auto result = rasterizer->render(group.release(), outputExtent);

        // Since we join on the rasterizer future immediately, we need to make sure
        // the join cancels properly with a custom cancel predicate that checks for
        // the existence and status of the host layer.
        osg::observer_ptr<const Layer> layer(this);

        osg::ref_ptr<ProgressCallback> local_progress = new ProgressCallback(
            progress,
            [layer]() {
                osg::ref_ptr<const Layer> safe;
                return !layer.lock(safe) || !safe->isOpen() || !jobs::alive();
            }
        );

        // Immediately blocks on the result.
        // That is OK - we are hopefully in a loading thread.
        osg::ref_ptr<osg::Image> image = result.join(local_progress.get());

        // Empty image means the texture did not render anything
        if (image.valid() && image->data() != nullptr)
        {
            if (!ImageUtils::isEmptyImage(image.get()))
            {
                return GeoImage(image.get(), key.getExtent());
            }
            else
            {
                //OE_DEBUG << LC << "RSL: skipped an EMPTY image result for " << key.str() << std::endl;
                return GeoImage::INVALID;
            }
        }
        else
        {
            return GeoImage::INVALID;
        }
    }

    return GeoImage::INVALID;
}
