// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: jproc

/*! \file WallData.h
    \brief Contains declarations for all types (currently Sphere, Cylinder, and
    Plane) of WallData and associated utilities.
 */
#ifndef WALL_DATA_H
#define WALL_DATA_H

#include "hoomd/HOOMDMath.h"
#include "hoomd/VectorMath.h"
#include "hoomd/BoxDim.h"

#ifdef NVCC
#define DEVICE __device__
#else
#define DEVICE
#endif

//! SphereWall Constructor
/*! \param r Radius of the sphere
    \param origin The x,y,z coordinates of the center of the sphere
    \param inside Determines which half space is evaluated.
*/

#ifdef SINGLE_PRECISION
#define ALIGN_SCALAR 4
#else
#define ALIGN_SCALAR 8
#endif

struct SphereWall
    {
    SphereWall(Scalar rad = 0.0, Scalar3 orig = make_scalar3(0.0,0.0,0.0), bool ins = true) : origin(vec3<Scalar>(orig)), r(rad), inside(ins) {}
    vec3<Scalar>    origin; // need to order datatype in descending order of type size for Fermi
    Scalar          r;
    bool            inside;
    } __attribute__((aligned(ALIGN_SCALAR))); // align according to first member of vec3<Scalar>

//! CylinderWall Constructor
/*! \param r Radius of the sphere
    \param origin The x,y,z coordinates of a point on the cylinder axis
    \param axis A x,y,z vector along the cylinder axis used to define the axis
    \param quatAxisToZRot (Calculated not input) The quaternion which rotates the simulation space such that the axis of the cylinder is parallel to the z' axis
    \param inside Determines which half space is evaluated.
*/
struct CylinderWall
    {
    CylinderWall(Scalar rad = 0.0, Scalar3 orig = make_scalar3(0.0,0.0,0.0), Scalar3 zorient = make_scalar3(0.0,0.0,1.0), bool ins=true) : origin(vec3<Scalar>(orig)), axis(vec3<Scalar>(zorient)), r(rad), inside(ins)
        {
        vec3<Scalar> zVec=axis;
        vec3<Scalar> zNorm(0.0,0.0,1.0);

        // method source: http://lolengine.net/blog/2014/02/24/quaternion-from-two-vectors-final
        // easily simplified due to zNorm being a normalized vector
        Scalar normVec=sqrt(dot(zVec,zVec));
        Scalar realPart=normVec + dot(zNorm,zVec);
        vec3<Scalar> w;

        if (realPart < Scalar(1.0e-6) * normVec)
            {
                realPart=Scalar(0.0);
                w=vec3<Scalar>(0.0, -1.0, 0.0);
            }
        else
            {
                w=cross(zNorm,zVec);
                realPart=Scalar(realPart);
            }
            quatAxisToZRot=quat<Scalar>(realPart,w);
            Scalar norm=fast::rsqrt(norm2(quatAxisToZRot));
            quatAxisToZRot=norm*quatAxisToZRot;
        }
    quat<Scalar>    quatAxisToZRot; // need to order datatype in descending order of type size for Fermi
    vec3<Scalar>    origin;
    vec3<Scalar>    axis;
    Scalar          r;
    bool            inside;
    } __attribute__((aligned(ALIGN_SCALAR))); // align according to first member of quat<Scalar>

//! PlaneWall Constructor
/*! \param origin The x,y,z coordinates of a point on the cylinder axis
    \param normal The x,y,z normal vector of the plane (normalized upon input)
    \param inside Determines which half space is evaluated.
*/
struct PlaneWall
    {
    PlaneWall(Scalar3 orig = make_scalar3(0.0,0.0,0.0), Scalar3 norm = make_scalar3(0.0,0.0,1.0), bool ins = true) : normal(vec3<Scalar>(norm)), origin(vec3<Scalar>(orig)), inside(ins)
        {
        vec3<Scalar> nVec;
        nVec = normal;
        Scalar invNormLength;
        invNormLength=fast::rsqrt(nVec.x*nVec.x + nVec.y*nVec.y + nVec.z*nVec.z);
        normal=nVec*invNormLength;
        }
    vec3<Scalar>    normal;
    vec3<Scalar>    origin;
    bool            inside;
    } __attribute__((aligned(ALIGN_SCALAR))); // align according to first member of vec3<Scalar>

//! Point to wall vector for a sphere wall geometry
/* Returns 0 vector when all normal directions are equal
*/
DEVICE inline vec3<Scalar> vecPtToWall(const SphereWall& wall, const vec3<Scalar>& position, bool& inside)
    {
    vec3<Scalar> t = position;
    t-=wall.origin;
    vec3<Scalar> shiftedPos(t);
    Scalar rxyz = sqrt(dot(shiftedPos,shiftedPos));
    if (rxyz > 0.0)
        {
        inside = (((rxyz <= wall.r) && wall.inside) || ((rxyz > wall.r) && !(wall.inside))) ? true : false;
        t *= wall.r/rxyz;
        vec3<Scalar> dx = t - shiftedPos;
        return dx;
        }
    else
        {
        inside = (wall.inside) ? true : false;
        return vec3<Scalar>(0.0,0.0,0.0);
        }
    };

//! Point to wall vector for a cylinder wall geometry
/* Returns 0 vector when all normal directions are equal
*/
DEVICE inline vec3<Scalar> vecPtToWall(const CylinderWall& wall, const vec3<Scalar>& position, bool& inside)
    {
    vec3<Scalar> t = position;
    t-=wall.origin;
    vec3<Scalar> shiftedPos = rotate(wall.quatAxisToZRot,t);
    shiftedPos.z = 0.0;
    Scalar rxy = sqrt(dot(shiftedPos,shiftedPos));
    if (rxy > 0.0)
        {
        inside = (((rxy <= wall.r) && wall.inside) || ((rxy > wall.r) && !(wall.inside))) ? true : false;
        t = (wall.r / rxy) * shiftedPos;
        vec3<Scalar> dx = t - shiftedPos;
        dx = rotate(conj(wall.quatAxisToZRot),dx);
        return dx;
        }
    else
        {
        inside = (wall.inside) ? true : false;
        return vec3<Scalar>(0.0,0.0,0.0);
        }
    };

//! Point to wall vector for a plane wall geometry
DEVICE inline vec3<Scalar> vecPtToWall(const PlaneWall& wall, const vec3<Scalar>& position, bool& inside)
    {
    vec3<Scalar> t = position;
    Scalar d = dot(wall.normal,t) - dot(wall.normal,wall.origin);
    inside = (((d >= 0.0) && wall.inside) || ((d < 0.0) && !(wall.inside))) ? true : false;
    vec3<Scalar> dx = -d * wall.normal;
    return dx;
    };

//! Distance of point to inside sphere wall geometry, not really distance, +- based on if it's inside or not
DEVICE inline Scalar distWall(const SphereWall& wall, const vec3<Scalar>& position)
    {
    vec3<Scalar> t = position;
    t-=wall.origin;
    vec3<Scalar> shiftedPos(t);
    Scalar rxyz2 = shiftedPos.x*shiftedPos.x + shiftedPos.y*shiftedPos.y + shiftedPos.z*shiftedPos.z;
    Scalar d = wall.r - sqrt(rxyz2);
    d = (wall.inside) ? d : -d;
    return d;
    };

//! Distance of point to inside cylinder wall geometry, not really distance, +- based on if it's inside or not
DEVICE inline Scalar distWall(const CylinderWall& wall, const vec3<Scalar>& position)
    {
    vec3<Scalar> t = position;
    t-=wall.origin;
    vec3<Scalar> shiftedPos=rotate(wall.quatAxisToZRot,t);
    Scalar rxy2= shiftedPos.x*shiftedPos.x + shiftedPos.y*shiftedPos.y;
    Scalar d = wall.r - sqrt(rxy2);
    d = (wall.inside) ? d : -d;
    return d;
    };

//! Distance of point to inside plane wall geometry, not really distance, +- based on if it's inside or not
DEVICE inline Scalar distWall(const PlaneWall& wall, const vec3<Scalar>& position)
    {
    vec3<Scalar> t = position;
    Scalar d = dot(wall.normal,t) - dot(wall.normal,wall.origin);
    d = (wall.inside) ? d : -d;
    return d;
    };

//! Method for rescaling the plane wall properties iteratively
// inline void rescaleWall()
//Andres:Rescale Plane Walls
inline void getTransMatrix(const BoxDim& old_box, const BoxDim& new_box, Scalar *A )
    {
    //Get the Column Vectors of the old and new box
    //Old Box
    Scalar3 a_old = old_box.getLatticeVector(0);
    Scalar3 b_old = old_box.getLatticeVector(1);
    Scalar3 c_old = old_box.getLatticeVector(2);
    //New Box
    Scalar3 a_new = new_box.getLatticeVector(0);
    Scalar3 b_new = new_box.getLatticeVector(1);
    Scalar3 c_new = new_box.getLatticeVector(2);

    //Calculate the inverse of old matrix
    //Conventional Formula to get Inverse Matrix
    Scalar inv11 = b_old.y * c_old.z - c_old.y * b_old.z;
    Scalar inv12 = c_old.x * b_old.z - b_old.x * c_old.z;
    Scalar inv13 = b_old.x * c_old.y - c_old.x * b_old.y;
    Scalar inv21 = c_old.y * a_old.z - a_old.y * c_old.z;
    Scalar inv22 = a_old.x * c_old.z - c_old.x * a_old.z;
    Scalar inv23 = c_old.x * a_old.y - a_old.x * c_old.y;
    Scalar inv31 = a_old.y * b_old.z - b_old.y * a_old.z;
    Scalar inv32 = b_old.x * a_old.z - a_old.x * b_old.z;
    Scalar inv33 = a_old.x * b_old.y - b_old.x * a_old.y;

    Scalar detinv = a_old.x * inv11 + b_old.x * inv21 + c_old.x * inv31;
    inv11 /= detinv;
    inv12 /= detinv;
    inv13 /= detinv;
    inv21 /= detinv;
    inv22 /= detinv;
    inv23 /= detinv;
    inv31 /= detinv;
    inv32 /= detinv;
    inv33 /= detinv;

    //Calculate transformation matrix elements
    // A = new_box_matrix * inverse(old_box_matrix)
    //First Row of elements
    A[0] = a_new.x*inv11 + b_new.x*inv21 + c_new.x*inv31;
    A[1] = a_new.x*inv12 + b_new.x*inv22 + c_new.x*inv32;
    A[2] = a_new.x*inv13 + b_new.x*inv23 + c_new.x*inv33;


    //Second Row of elements
    A[3] = a_new.y*inv11 + b_new.y*inv21 + c_new.y*inv31;
    A[4] = a_new.y*inv12 + b_new.y*inv22 + c_new.y*inv32;
    A[5] = a_new.y*inv13 + b_new.y*inv23 + c_new.y*inv33;


    //Third Row of elements
    A[6] = a_new.z*inv11 + b_new.z*inv21 + c_new.z*inv31;
    A[7] = a_new.z*inv12 + b_new.z*inv22 + c_new.z*inv32;
    A[8] = a_new.z*inv13 + b_new.z*inv23 + c_new.z*inv33;
    }

//inline void rescaleWall( PlaneWall& wall, const BoxDim& old_box,const BoxDim& new_box)
inline void rescaleWall(PlaneWall& wall, const BoxDim& old_box, const Scalar *transMatrix )
    {
    //!Rescale Wall origin and center using transformation matrix


		//rescale origin

    

    wall.origin.x = wall.origin.x * transMatrix[0] + wall.origin.y * transMatrix[1] +wall.origin.z * transMatrix[2];
    wall.origin.y = wall.origin.x * transMatrix[3] + wall.origin.y * transMatrix[4] +wall.origin.z * transMatrix[5];
    wall.origin.z = wall.origin.x * transMatrix[6] + wall.origin.y * transMatrix[7] +wall.origin.z * transMatrix[8];



    //rescale normal 
    // find transpose inverse of transMatrix

    Scalar inv11 = transMatrix[4] * transMatrix[8] - transMatrix[5] * transMatrix[7];
    Scalar inv12 = transMatrix[2] * transMatrix[7] - transMatrix[1] * transMatrix[8];
    Scalar inv13 = transMatrix[1] * transMatrix[5] - transMatrix[2] * transMatrix[4];
    Scalar inv21 = transMatrix[5] * transMatrix[6] - transMatrix[3] * transMatrix[8];
    Scalar inv22 = transMatrix[0] * transMatrix[8] - transMatrix[2] * transMatrix[6];
    Scalar inv23 = transMatrix[2] * transMatrix[3] - transMatrix[0] * transMatrix[5];
    Scalar inv31 = transMatrix[3] * transMatrix[7] - transMatrix[4] * transMatrix[6];
    Scalar inv32 = transMatrix[1] * transMatrix[6] - transMatrix[0] * transMatrix[7];
    Scalar inv33 = transMatrix[0] * transMatrix[4] - transMatrix[1] * transMatrix[3];

    Scalar detinv = transMatrix[0] * inv11 + transMatrix[1] * inv21 + transMatrix[2] * inv31;
    inv11 /= detinv;
    inv12 /= detinv;
    inv13 /= detinv;
    inv21 /= detinv;
    inv22 /= detinv;
    inv23 /= detinv;
    inv31 /= detinv;
    inv32 /= detinv;
    inv33 /= detinv;


    wall.normal.x = wall.normal.x * inv11 + wall.normal.y * inv21 +wall.normal.z * inv31;
    wall.normal.y = wall.normal.x * inv12 + wall.normal.y * inv22 +wall.normal.z * inv32;
    wall.normal.z = wall.normal.x * inv13 + wall.normal.y * inv23 +wall.normal.z * inv33;


    Scalar invNormLength=fast::rsqrt(wall.normal.x*wall.normal.x + wall.normal.y*wall.normal.y + wall.normal.z*wall.normal.z);

    wall.normal=wall.normal*invNormLength;


    };


#endif
