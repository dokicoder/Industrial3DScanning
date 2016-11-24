#ifndef VERTEX_H
#define VERTEX_H

#include <QVector3D>

struct Vertex {
    /*
	operator QVector3D&()
    {
        return position;
    }
	*/

    Vertex()
    {
        color.setX(1);
        color.setY(1);
        color.setZ(1);
    }

    Vertex(const QVector3D& position): position(position)
    {
        color.setX(1);
        color.setY(1);
        color.setZ(1);
    }

    Vertex(const QVector3D& position, const QVector3D& color): position(position), color(color)
    {}

    Vertex(const QVector3D& position, const QVector3D& normal, const QVector3D& color):
        position(position), normal(normal), color(color)
    {}

    operator QVector3D&() { return position; }

    // NaN in color Z component is used as flag for a generic markings of a vertex
    void flag()
    {
        color.setZ( std::numeric_limits<float>::quiet_NaN() );
    }
    bool isFlagged()
    {
        return std::isnan( color.z() );
    }

    QVector3D position;
    QVector3D normal;
    QVector3D color;
};

#endif // VERTEX_H
