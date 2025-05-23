/* osgEarth
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include <osgEarth/IntersectionPicker>
#include <osgEarth/Registry>

#define LC "[Picker] "

using namespace osgEarth;


IntersectionPicker::IntersectionPicker( osgViewer::View* view, osg::Node* root, unsigned travMask, float buffer, Limit limit ) :
_view    ( view ),
_root    ( root ),
_travMask( travMask ),
_buffer  ( buffer ),
_limit   ( limit )
{
    if ( root )
        _path = root->getParentalNodePaths()[0];
}

void
IntersectionPicker::setLimit(const IntersectionPicker::Limit& value)
{
    _limit = value;
}

void
IntersectionPicker::setTraversalMask(unsigned value)
{
    _travMask = value;
}

void
IntersectionPicker::setBuffer(float value)
{
    _buffer = value;
}

bool
IntersectionPicker::pick( float x, float y, Hits& results ) const
{
    float local_x, local_y = 0.0;
    const osg::Camera* camera = _view->getCameraContainingPosition(x, y, local_x, local_y);
    if ( !camera )
        camera = _view->getCamera();

    osg::ref_ptr<osgEarth::PrimitiveIntersector> picker;

    double buffer_x = _buffer, buffer_y = _buffer;
    if ( camera->getViewport() )
    {
        double aspectRatio = camera->getViewport()->width()/camera->getViewport()->height();
        buffer_x *= aspectRatio;
        buffer_y /= aspectRatio;
    }

    osg::Matrix windowMatrix;

    if ( _root.valid() )
    {
        osg::Matrix modelMatrix;

        if (camera->getViewport())
        {
            windowMatrix = camera->getViewport()->computeWindowMatrix();
            modelMatrix.preMult( windowMatrix );
        }

        modelMatrix.preMult( camera->getProjectionMatrix() );
        modelMatrix.preMult( camera->getViewMatrix() );

        osg::NodePath prunedNodePath( _path.begin(), _path.end()-1 );
        modelMatrix.preMult( osg::computeWorldToLocal(prunedNodePath) );

        osg::Matrix modelInverse;
        modelInverse.invert(modelMatrix);

        osg::Vec3d startLocal(local_x, local_y, 0.0);
        osg::Vec3d startModel = startLocal * modelInverse;

        osg::Vec3d endLocal(local_x, local_y, 1.0);
        osg::Vec3d endModel = endLocal * modelInverse;

        osg::Vec3d bufferLocal(local_x + buffer_x, local_y + buffer_y, 0.0);
        osg::Vec3d bufferModel = bufferLocal * modelInverse;

        double buffer = std::max((bufferModel - startModel).length(), 5.0);  //TODO: Setting a minimum of 5.0 may need revisited

        OE_DEBUG
            << "local_x:" << local_x << ", local_y:" << local_y
            << ", buffer_x:" << buffer_x << ", buffer_y:" << buffer_y
            << ", bm.x:" << bufferModel.x() << ", bm.y:" << bufferModel.y()
            << ", bm.z:" << bufferModel.z()
            << ", BUFFER: " << buffer
            << std::endl;

        picker = new osgEarth::PrimitiveIntersector(osgUtil::Intersector::MODEL, startModel, endModel, buffer);
    }
    else
    {
        picker = new osgEarth::PrimitiveIntersector(camera->getViewport() ? osgUtil::Intersector::WINDOW : osgUtil::Intersector::PROJECTION, local_x, local_y, _buffer);
    }

    picker->setIntersectionLimit( (osgUtil::Intersector::IntersectionLimit)_limit );
    osgUtil::IntersectionVisitor iv(picker.get());

    //picker->setIntersectionLimit( osgUtil::Intersector::LIMIT_ONE_PER_DRAWABLE );

    // in MODEL mode, we need to window and proj matrixes in order to support some of the 
    // features in osgEarth (like Annotation::GeoPositionNode).
    if ( _root.valid() )
    {
        iv.pushWindowMatrix( new osg::RefMatrix(windowMatrix) );
        iv.pushProjectionMatrix( new osg::RefMatrix(camera->getProjectionMatrix()) );
        iv.pushViewMatrix( new osg::RefMatrix(camera->getViewMatrix()) );
    }

    iv.setTraversalMask( _travMask );

    if ( _root.valid() )
        _path.back()->accept(iv);
    else
        const_cast<osg::Camera*>(camera)->accept(iv);

    if (picker->containsIntersections())
    {
        results = picker->getIntersections();
        return true;
    }
    else
    {
        results.clear();
        return false;
    }
}

bool
IntersectionPicker::getObjectIDs(const Hits& results, std::set<ObjectID>& out_objectIDs) const
{
    ObjectIndex* index = Registry::objectIndex();

    for(Hits::const_iterator hit = results.begin(); hit != results.end(); ++hit)
    {
        bool found = false;

        // check for the uniform.
        const osg::NodePath& path = hit->nodePath;
        for(osg::NodePath::const_reverse_iterator n = path.rbegin(); n != path.rend(); ++n )
        {
            osg::Node* node = *n;
            if ( node && node->getStateSet() )
            {
                osg::Uniform* u = node->getStateSet()->getUniform( index->getObjectIDUniformName() );
                if ( u )
                {
                    ObjectID oid;
                    if ( u->get(oid) )
                    {
                        out_objectIDs.insert( oid );
                        found = true;
                    }
                }
            }
        }

        if ( !found )
        {
            // check the geometry.
            const osg::Geometry* geom = hit->drawable ? hit->drawable->asGeometry() : 0L;
            if ( geom )
            {
                const ObjectIDArray* ids = dynamic_cast<const ObjectIDArray*>( geom->getVertexAttribArray(index->getObjectIDAttribLocation()) );
                if ( ids )
                {
                    for(unsigned i=0; i < hit->indexList.size(); ++i)
                    {
                        unsigned index = hit->indexList[i];
                        if ( index < ids->size() )
                        {
                            ObjectID oid = (*ids)[index];
                            out_objectIDs.insert( oid );
                        }
                    }
                }
            }
        }
    }

    return !out_objectIDs.empty();
}
