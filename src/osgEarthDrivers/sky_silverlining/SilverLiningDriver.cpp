/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
 * Copyright 2020 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include <osgEarthSilverLining/SilverLiningOptions>
#include <osgEarthSilverLining/SilverLiningNode>
#include <osgDB/FileNameUtils>
#include <osgEarth/Map>
#include <osgEarth/MapNode>
#include <osgEarth/NodeUtils>
#include <osgEarth/ExampleResources>

#ifdef OSGEARTH_HAVE_CONTROLS_API
#include <osgEarth/Controls>
namespace ui = osgEarth::Util::Controls;
#endif

#define LC "[SilverLiningExtension] "

using namespace osgEarth::Util;

namespace osgEarth { namespace SilverLining
{
#ifdef OSGEARTH_HAVE_CONTROLS_API
    class SilverLiningExtension :
        public Extension,
        public ExtensionInterface<MapNode>,
        public ExtensionInterface<osg::View>,
        public ExtensionInterface<ui::Control>,
        public SilverLiningOptions,
        public SkyNodeFactory
#else
    class SilverLiningExtension :
        public Extension,
        public ExtensionInterface<MapNode>,
        public ExtensionInterface<osg::View>,
        public SilverLiningOptions,
        public SkyNodeFactory
#endif
    {
    public:
        META_OE_Extension(osgEarth, SilverLiningExtension, sky_silverlining);

        // CTORs
        SilverLiningExtension() { }

        SilverLiningExtension(const ConfigOptions& options) :
            SilverLiningOptions(options) { }

    public: // Extension

        const ConfigOptions& getConfigOptions() const { return *this; }


    public: // ExtensionInterface<MapNode>

        bool connect(MapNode* mapNode)
        {
            _skynode = createSkyNode();
            if (mapNode->getMapSRS()->isProjected())
            {
                GeoPoint refPoint = 
                    mapNode->getMap()->getProfile()->getExtent().getCentroid();
                _skynode->setReferencePoint(refPoint);
            }                
            osgEarth::insertParent(_skynode.get(), mapNode);
            return true;
        }

        bool disconnect(MapNode* mapNode)
        {
            //todo
            return true;
        }

    public: // ExtensionInterface<osg::View>

        bool connect(osg::View* view)
        {
            if (view && _skynode.valid())
            {
                _skynode->attach(view, 0);
            }
            return true;
        }

        bool disconnect(osg::View* view)
        {
            //todo
            return true;
        }

#ifdef OSGEARTH_HAVE_CONTROLS_API
    public: // ExtensionInterface<Control>

        bool connect(ui::Control* control)
        {
            //ui::Container* container = dynamic_cast<ui::Container*>(control);
            //if (container)
            //    container->addControl(SkyControlFactory::create(_skynode.get()));
            return true;
        }

        bool disconnect(ui::Control* control)
        {
            //todo
            return false;
        }
#endif

    public: // SkyNodeFactory

        SkyNode* createSkyNode() {
            return new SilverLiningNode(*this);
        }


    protected: // Object
        
        // DTOR
        virtual ~SilverLiningExtension() { }


    private:
        osg::ref_ptr<SkyNode> _skynode;
    };

    REGISTER_OSGEARTH_EXTENSION(osgearth_sky_silverlining, SilverLiningExtension)
} }
