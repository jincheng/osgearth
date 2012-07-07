/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2012 Pelican Mapping
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
#include <osgEarth/Draggers>
#include <osgEarth/GeoMath>
#include <osgEarth/MapNode>
#include <osgEarth/Terrain>
#include <osgEarth/Pickers>

#include <osg/AutoTransform>
#include <osgViewer/View>

#include <osg/io_utils>

#include <osgGA/EventVisitor>



using namespace osgEarth;

struct ClampDraggerCallback : public TerrainCallback
{
    void onTileAdded( const TileKey& key, osg::Node* tile, TerrainCallbackContext& context )
    {
        Dragger* dragger = static_cast<Dragger*>(context.getClientData());
        dragger->reclamp( key, tile, context.getTerrain() );
    }
};

/**********************************************************/
Dragger::Dragger( MapNode* mapNode):
_mapNode( mapNode ),
_position( mapNode->getMapSRS(), 0,0,0, ALTMODE_RELATIVE),
_dragging(false),
_hovered(false),
_verticalOffset(0.0),
_modKeyMask(-1)
{
    setNumChildrenRequiringEventTraversal( 1 );

    _mapNode->getTerrain()->addTerrainCallback( new ClampDraggerCallback(), this ); 
}

Dragger::~Dragger()
{
    osg::ref_ptr< MapNode > mapNode = _mapNode;
    if (mapNode.valid())
    {
        mapNode->getTerrain()->removeTerrainCallbacksWithClientData(this);
    }
}

bool Dragger::getDragging() const
{
    return _dragging;
}

bool Dragger::getHovered() const
{
    return _hovered;
}

const GeoPoint& Dragger::getPosition() const
{
    return _position;
}

void Dragger::setPosition( const GeoPoint& position, bool fireEvents)
{
    if (_position != position)
    {
        _position = position;
        updateTransform();

        if (fireEvents)
        {
            for( PositionChangedCallbackList::iterator i = _callbacks.begin(); i != _callbacks.end(); i++ )
            {
                i->get()->onPositionChanged(this, _position);
            }
        }
    }
}

void Dragger::setVerticalOffset( double offset, bool fireEvents )
{
  if (_verticalOffset != offset)
  {
      _verticalOffset = offset;

      if (fireEvents)
      {
          for( PositionChangedCallbackList::iterator i = _callbacks.begin(); i != _callbacks.end(); i++ )
          {
              i->get()->onVerticalOffsetChanged(this, _verticalOffset);
          }
      }
  }
}

void Dragger::getOffsetPosition(GeoPoint& out_position) const
{
    out_position.set(_position.getSRS(), _position.x(), _position.y(), _position.z() + _verticalOffset, _position.altitudeMode());
}

void Dragger::updateTransform(osg::Node* patch)
{
    osg::Matrixd matrix;


    GeoPoint mapPoint( _position );
    mapPoint.makeAbsolute( _mapNode->getTerrain() );
    //mapPoint.fromWorld( _mapNode->getMapSRS(), _position );
    
#if 0
    //Get the height
    if (_position.altitudeMode() == ALTMODE_RELATIVE)
    {
        double hamsl;
        if (_mapNode->getTerrain()->getHeight(patch, mapPoint.getSRS(), mapPoint.x(), mapPoint.y(), &hamsl, 0L))
        {
            mapPoint.z() += hamsl;
        }
        mapPoint.altitudeMode() = ALTMODE_ABSOLUTE;
    }            
#endif

    mapPoint.createLocalToWorld( matrix );
    setMatrix( matrix );
}

void Dragger::enter()
{
}

void Dragger::leave()
{        
}    

void Dragger::addPositionChangedCallback( PositionChangedCallback* callback )
{
    _callbacks.push_back( callback );
}

void Dragger::removePositionChangedCallback( PositionChangedCallback* callback )
{
    PositionChangedCallbackList::iterator i = std::find( _callbacks.begin(), _callbacks.end(), callback);
    if (i != _callbacks.end())
    {
        _callbacks.erase( i );
    }    
}

void Dragger::traverse(osg::NodeVisitor& nv)
{
    if (nv.getVisitorType() == osg::NodeVisitor::EVENT_VISITOR)
    {
        osgGA::EventVisitor* ev = static_cast<osgGA::EventVisitor*>(&nv);
        for(osgGA::EventQueue::Events::iterator itr = ev->getEvents().begin();
            itr != ev->getEvents().end();
            ++itr)
        {
            osgGA::GUIEventAdapter* ea = itr->get();
            if (handle(*ea, *(ev->getActionAdapter()))) ea->setHandled(true);
        }
    }
    osg::MatrixTransform::traverse(nv);
}

bool Dragger::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
{
    if (ea.getHandled()) return false;

    osgViewer::View* view = dynamic_cast<osgViewer::View*>(&aa);
    if (!view) return false;
    if (!_mapNode.valid()) return false;

    if (ea.getEventType() == osgGA::GUIEventAdapter::PUSH)
    {
        Picker picker( view, this );
        Picker::Hits hits;

        if ( picker.pick( ea.getX(), ea.getY(), hits ) )
        {
            _dragging = true;
            _startY = ea.getY();
            _startAlt = _position.alt();
            _startOffset = _verticalOffset;

            //Calculate the screen-to-world ratio
            osg::Vec3d world1, world2;
            if (_mapNode->getTerrain()->getWorldCoordsUnderMouse(view, ea.getX(), ea.getY(), world1) &&
                _mapNode->getTerrain()->getWorldCoordsUnderMouse(view, ea.getX(), ea.getY() - 1.0f, world2))
            {
              osgEarth::GeoPoint mapPoint1;
              mapPoint1.fromWorld(_mapNode->getMapSRS(), world1);

              osgEarth::GeoPoint mapPoint2;
              mapPoint2.fromWorld(_mapNode->getMapSRS(), world2);

              _verticalRatio = GeoMath::distance(mapPoint1.vec3d(), mapPoint2.vec3d(), _mapNode->getMapSRS());
            }
            else
            {
              _verticalRatio = 0;
            }

            aa.requestRedraw();
            return true;
        }
    }
    else if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE)
    {
        _dragging = false;
        aa.requestRedraw();
    }
    else if (ea.getEventType() == osgGA::GUIEventAdapter::DRAG)
    {
        if (_dragging)
        {
            if(_modKeyMask > 0 && ea.getModKeyMask() & _modKeyMask)
            {
              //double alt = _startAlt + (ea.getY() - _startY) * _verticalRatio;

              //if (_position.altitudeMode() == ALTMODE_RELATIVE && alt < 0.0)
              //  alt = 0.0;

              //GeoPoint mapPoint(_mapNode->getMapSRS(), _position.x(), _position.y(), alt, _position.altitudeMode());
              //setPosition(mapPoint);

              double offset = _startOffset + (ea.getY() - _startY) * _verticalRatio;
              setVerticalOffset(offset);

              aa.requestRedraw();
              return true;
            }
            else
            {
                osg::Vec3d world;
                if ( _mapNode->getTerrain()->getWorldCoordsUnderMouse(view, ea.getX(), ea.getY(), world) )
                {
                    //Get the absolute mapPoint that they've drug it to.
                    GeoPoint mapPoint;
                    mapPoint.fromWorld( _mapNode->getMapSRS(), world );
                    //_mapNode->getMap()->worldPointToMapPoint(world, mapPoint);

                    //If the current position is relative, we need to convert the absolute world point to relative.
                    //If the point is absolute then just emit the absolute point.
                    if (_position.altitudeMode() == ALTMODE_RELATIVE)
                    {
                        mapPoint.alt() = _position.alt();
                        mapPoint.altitudeMode() = ALTMODE_RELATIVE;
                    }
                    setPosition( mapPoint );
                    aa.requestRedraw();
                    return true;
                }
            }
        }
    }   
    else if (ea.getEventType() == osgGA::GUIEventAdapter::MOVE)
    {
        Picker picker( view, this );
        Picker::Hits hits;

        if ( picker.pick( ea.getX(), ea.getY(), hits ) )
        {
            setHover( true );
        }
        else
        {
            setHover( false );
        }        
        aa.requestRedraw();
    }
    return false;
}

void Dragger::setHover( bool hovered)
{
    if (_hovered != hovered)
    {
        bool wasHovered = _hovered;
        _hovered = hovered;
        if (wasHovered)
        {
            leave();            
        }
        else
        {
            enter();
        }
    }
}

void Dragger::reclamp( const TileKey& key, osg::Node* tile, const Terrain* terrain )
{    
    GeoPoint p;
    _position.transform( key.getExtent().getSRS(), p );
    // first verify that the control position intersects the tile:
    if ( key.getExtent().contains( p.x(), p.y() ) )
    {
        updateTransform( tile );
    }
}



/**********************************************************/

SphereDragger::SphereDragger(MapNode* mapNode):
Dragger( mapNode ),
_pickColor(1.0f, 1.0f, 0.0f, 1.0f),
_color(0.0f, 1.0f, 0.0f, 1.0f),
_size( 5.0f )
{
    //Disable culling
    setCullingActive( false );

    //Build the handle
    osg::Sphere* shape = new osg::Sphere(osg::Vec3(0,0,0), 1.0f);   
    osg::Geode* geode = new osg::Geode();
    _shapeDrawable = new osg::ShapeDrawable( shape );    
    _shapeDrawable->setDataVariance( osg::Object::DYNAMIC );
    geode->addDrawable( _shapeDrawable );          

    geode->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    geode->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    _scaler = new osg::MatrixTransform;
    _scaler->setMatrix( osg::Matrixd::scale( _size, _size, _size ));
    _scaler->addChild( geode );

    osg::AutoTransform* at = new osg::AutoTransform;
    at->setAutoScaleToScreen( true );
    at->addChild( _scaler );
    addChild( at );

    updateColor();
}

const osg::Vec4f& SphereDragger::getColor() const
{
    return _color;
}

void SphereDragger::setColor(const osg::Vec4f& color)
{
    if (_color != color)
    {
        _color = color;
        updateColor();
    }
}


const osg::Vec4f& SphereDragger::getPickColor() const
{
    return _pickColor;
}

void SphereDragger::setPickColor(const osg::Vec4f& pickColor)
{
    if (_pickColor != pickColor)
    {
        _pickColor = pickColor;
        updateColor();
    }
}

float SphereDragger::getSize() const
{
    return _size;
}

void SphereDragger::setSize(float size)
{
    if (_size != size)
    {
        _size = size;
        _scaler->setMatrix( osg::Matrixd::scale( _size, _size, _size ));
    }
}


void SphereDragger::enter()
{
    updateColor();
}

void SphereDragger::leave()
{
    updateColor();
}

void SphereDragger::updateColor()
{
    if (getHovered())
    {
        _shapeDrawable->setColor( _pickColor );
    }        
    else
    {
        _shapeDrawable->setColor( _color );
    }
}
