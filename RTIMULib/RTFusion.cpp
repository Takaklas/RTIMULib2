////////////////////////////////////////////////////////////////////////////
//
//  This file is part of RTIMULib
//
//  Copyright (c) 2014-2015, richards-tech, LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy of
//  this software and associated documentation files (the "Software"), to deal in
//  the Software without restriction, including without limitation the rights to use,
//  copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
//  Software, and to permit persons to whom the Software is furnished to do so,
//  subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
//  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
//  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "RTFusion.h"
#include "RTIMUHal.h"

//  The slerp power valule controls the influence of the measured state to correct the predicted state
//  0 = measured state ignored (just gyros), 1 = measured state overrides predicted state.
//  In between 0 and 1 mixes the two conditions

#define RTQF_SLERP_POWER (RTFLOAT)0.02;

const char *RTFusion::m_fusionNameMap[] = {
    "NULL",
    "Kalman STATE4",
    "RTQF",
    "Madgwick",
    "Mahony"};

RTFusion::RTFusion()
{
    m_debug = false;
    m_firstTime = true;
    m_enableGyro = true;
    m_enableAccel = true;
    m_enableCompass = true;

    m_slerpPower = RTQF_SLERP_POWER;
}

RTFusion::~RTFusion()
{
}

void RTFusion::calculatePose(const RTVector3& accel, const RTVector3& mag, float magDeclination)
{
    RTQuaternion m;
    RTQuaternion q;

    if (m_enableAccel) {
        accel.accelToEuler(m_measuredPose);
    } else {
        m_measuredPose = m_fusionPose;
        m_measuredPose.setZ(0);
    }

    if (m_enableCompass && m_compassValid) {
        q.fromEuler(m_measuredPose);
        m.setScalar(0);
        m.setX(mag.x());
        m.setY(mag.y());
        m.setZ(mag.z());

        m = q * m * q.conjugate();
        m_measuredPose.setZ(-atan2(m.y(), m.x()) - magDeclination);
    } else {
        m_measuredPose.setZ(m_fusionPose.z());
    }

    m_measuredQPose.fromEuler(m_measuredPose);

    //  check for quaternion aliasing. If the quaternion has the wrong sign
    //  the kalman filter will be very unhappy.

    int maxIndex = -1;
    RTFLOAT maxVal = -1000;

    for (int i = 0; i < 4; i++) {
        if (fabs(m_measuredQPose.data(i)) > maxVal) {
            maxVal = fabs(m_measuredQPose.data(i));
            maxIndex = i;
        }
    }

    //  if the biggest component has a different sign in the measured and kalman poses,
    //  change the sign of the measured pose to match.

    if (((m_measuredQPose.data(maxIndex) < 0) && (m_fusionQPose.data(maxIndex) > 0)) ||
            ((m_measuredQPose.data(maxIndex) > 0) && (m_fusionQPose.data(maxIndex) < 0))) {
        m_measuredQPose.setScalar(-m_measuredQPose.scalar());
        m_measuredQPose.setX(-m_measuredQPose.x());
        m_measuredQPose.setY(-m_measuredQPose.y());
        m_measuredQPose.setZ(-m_measuredQPose.z());
        m_measuredQPose.toEuler(m_measuredPose);
    }
}


RTVector3 RTFusion::getAccelResiduals(RTVector3 accel)
{
    RTQuaternion rotatedGravity;
    RTQuaternion fusedConjugate;
    RTQuaternion qTemp;
    RTVector3 residuals;

    //  do gravity rotation and subtraction

    // create the conjugate of the pose

    fusedConjugate = m_fusionQPose.conjugate();

    // now do the rotation - takes two steps with qTemp as the intermediate variable
    RTQuaternion gravity(0, 0, 0, 1);
    qTemp = gravity * m_fusionQPose;
    rotatedGravity = fusedConjugate * qTemp;

    // now adjust the measured accel and change the signs to make sense

    residuals.setX(-(accel.x() - rotatedGravity.x()));
    residuals.setY(-(accel.y() - rotatedGravity.y()));
    residuals.setZ(-(accel.z() - rotatedGravity.z()));
    return residuals;
}

RTVector3 RTFusion::getAccelGlobalFrame(RTVector3 accel)
{
    RTQuaternion rotatedAccel;
    RTQuaternion fusedConjugate;
    RTQuaternion qTemp;
    RTVector3 accelGCS;

    // simply rotate measured accel with the fusion pose quaternion

    // create the conjugate of the pose

    fusedConjugate = m_fusionQPose.conjugate();

    // now do the rotation - takes two steps with qTemp as the intermediate variable
    RTQuaternion measuredAccel(0, accel.x(), accel.y(), accel.z());
    qTemp = measuredAccel * fusedConjugate;
    rotatedAccel = m_fusionQPose * qTemp;

    // now set the accel in Global Coordinate System as the rotated accel

    accelGCS.setX(rotatedAccel.x());
    accelGCS.setY(rotatedAccel.y());
    accelGCS.setZ(rotatedAccel.z());
    return accelGCS;
}
