/* osgEarth
* Copyright 2025 Pelican Mapping
* MIT License
*/

/**
 * Demonstrates how to place an object and control its position and
 * orientation by combining a GeoTransform and a PositionAttitudeTransform.
 */

#include <osg/PositionAttitudeTransform>
#include <osgDB/ReadFile>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgEarth/EarthManipulator>
#include <osgEarth/ExampleResources>
#include <osgEarth/Controls>
#include <osgEarth/GeoTransform>
#include <osgEarth/MapNode>

#define LC "[osgearth_transform] "


using namespace osgEarth;
namespace ui = osgEarth::Util::Controls;

int
usage(const char* name)
{
    OE_NOTICE
        << "\nUsage: " << name << " file.earth" << std::endl;

    return 0;
}

struct App
{
    osgEarth::GeoTransform*           geo;
    osg::PositionAttitudeTransform*   pat;

    ui::HSliderControl* uiLat;
    ui::HSliderControl* uiLon;
    ui::HSliderControl* uiAlt;
    ui::HSliderControl* uiHeading;
    ui::HSliderControl* uiPitch;
    ui::HSliderControl* uiRoll;
    ui::CheckBoxControl* uiRelativeZ;

    void apply()
    {
        AltitudeMode altMode = uiRelativeZ->getValue() ? ALTMODE_RELATIVE : ALTMODE_ABSOLUTE;

        GeoPoint pos(
            SpatialReference::get("wgs84"),
            uiLon->getValue(), uiLat->getValue(), uiAlt->getValue(),
            altMode);

        geo->setPosition( pos );

        osg::Quat ori =
            osg::Quat(osg::DegreesToRadians(uiRoll->getValue()),    osg::Vec3(0,1,0)) *
            osg::Quat(osg::DegreesToRadians(uiPitch->getValue()),   osg::Vec3(1,0,0)) *
            osg::Quat(osg::DegreesToRadians(uiHeading->getValue()), osg::Vec3(0,0,-1));

        pat->setAttitude( ori );
    }
};

struct Apply : public ui::ControlEventHandler
{
    Apply(App& app) : _app(app) { }
    void onValueChanged(ui::Control* control) {
        _app.apply();
    }
    App& _app;
};

struct ZeroAlt : public ui::ControlEventHandler {
    ZeroAlt(App& app) : _app(app) { }
    void onClick(ui::Control* control) {
        _app.uiAlt->setValue(0.0f);
        _app.apply();
    }
    App& _app;
};

ui::Control* makeUI(App& app)
{
    ui::Grid* grid = new ui::Grid();
    grid->setBackColor(0,0,0,0.5);

    grid->setControl(0, 0, new ui::LabelControl("Lat:"));
    grid->setControl(0, 1, new ui::LabelControl("Long:"));
    grid->setControl(0, 2, new ui::LabelControl("Alt:"));
    grid->setControl(0, 3, new ui::LabelControl("Heading:"));
    grid->setControl(0, 4, new ui::LabelControl("Pitch:"));
    grid->setControl(0, 5, new ui::LabelControl("Roll:"));
    grid->setControl(0, 6, new ui::LabelControl("Relative Z:"));

    app.uiLat = grid->setControl(1, 0, new ui::HSliderControl(40.0f, 50.0f, 44.7433f, new Apply(app)));
    grid->setControl(2, 0, new ui::LabelControl(app.uiLat));
    app.uiLon = grid->setControl(1, 1, new ui::HSliderControl(6.0f, 8.0f, 7.0f, new Apply(app)));
    grid->setControl(2, 1, new ui::LabelControl(app.uiLon));
    app.uiAlt = grid->setControl(1, 2, new ui::HSliderControl(-3000.0f, 100000.0f, 25000.0f, new Apply(app)));
    grid->setControl(2, 2, new ui::LabelControl(app.uiAlt));
    grid->setControl(3, 2, new ui::ButtonControl("Zero", new ZeroAlt(app)));
    app.uiHeading = grid->setControl(1, 3, new ui::HSliderControl(-180.0f, 180.0f, 0.0f, new Apply(app)));
    grid->setControl(2, 3, new ui::LabelControl(app.uiHeading));
    app.uiPitch   = grid->setControl(1, 4, new ui::HSliderControl(-90.0f, 90.0f, 0.0f, new Apply(app)));
    grid->setControl(2, 4, new ui::LabelControl(app.uiPitch));
    app.uiRoll    = grid->setControl(1, 5, new ui::HSliderControl(-180.0f, 180.0f, 0.0f, new Apply(app)));
    grid->setControl(2, 5, new ui::LabelControl(app.uiRoll));
    app.uiRelativeZ = grid->setControl(1, 6, new ui::CheckBoxControl(true, new Apply(app))); app.uiRelativeZ->setWidth(15.0f);
    grid->setControl(2, 6, new ui::LabelControl(app.uiRelativeZ));

    app.uiLat->setHorizFill(true, 700.0f);
    return grid;
}

int
main(int argc, char** argv)
{
    osgEarth::initialize();

    osg::ArgumentParser arguments(&argc,argv);

    // help?
    if ( arguments.read("--help") )
        return usage(argv[0]);

    osgViewer::Viewer viewer(arguments);
    EarthManipulator* em = new EarthManipulator();
    viewer.setCameraManipulator( em );

    // load an earth file, and support all or our example command-line options
    // and earth file <external> tags
    auto earth = MapNodeHelper().load( arguments, &viewer );

    MapNode* mapNode = MapNode::get(earth);
    if (!mapNode)
        return usage(argv[0]);

    // load the model file into the local coordinate frame, which will be
    // +X=east, +Y=north, +Z=up.
    osg::ref_ptr<osg::Node> model = osgDB::readRefNodeFile("../data/axes.osgt.1000.scale.osgearth_shadergen");
    if (!model.valid())
        return usage(argv[0]);

    osg::Group* root = new osg::Group();
    root->addChild( earth );

    App app;
    app.geo = new GeoTransform();
    app.pat = new osg::PositionAttitudeTransform();
    app.pat->addChild( model.get() );
    app.geo->addChild( app.pat );

    // Place your GeoTransform under the map node and it will automatically support clamping.
    // If you don't do this, you must call setTerrain to get terrain clamping.
    mapNode->addChild( app.geo );

    viewer.setSceneData( root );

    ui::ControlCanvas::getOrCreate(&viewer)->addControl( makeUI(app) );
    app.apply();

    osgEarth::Viewpoint vp;
    vp.setNode( app.geo );
    vp.heading() = Angle( -45.0, Units::DEGREES );
    vp.pitch() = Angle( -20.0, Units::DEGREES );
    vp.range() = Distance( model->getBound().radius()*10.0, Units::METERS );
    em->setViewpoint( vp );

    return viewer.run();
}
