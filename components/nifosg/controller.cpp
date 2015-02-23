#include "controller.hpp"

#include <osg/MatrixTransform>
#include <osg/TexMat>
#include <osgAnimation/MorphGeometry>

#include <components/nif/data.hpp>

namespace NifOsg
{

float ValueInterpolator::interpKey(const Nif::FloatKeyMap::MapType &keys, float time, float def) const
{
    if (keys.size() == 0)
        return def;

    if(time <= keys.begin()->first)
        return keys.begin()->second.mValue;

    Nif::FloatKeyMap::MapType::const_iterator it = keys.lower_bound(time);
    if (it != keys.end())
    {
        float aTime = it->first;
        const Nif::FloatKey* aKey = &it->second;

        assert (it != keys.begin()); // Shouldn't happen, was checked at beginning of this function

        Nif::FloatKeyMap::MapType::const_iterator last = --it;
        float aLastTime = last->first;
        const Nif::FloatKey* aLastKey = &last->second;

        float a = (time - aLastTime) / (aTime - aLastTime);
        return aLastKey->mValue + ((aKey->mValue - aLastKey->mValue) * a);
    }
    else
        return keys.rbegin()->second.mValue;
}

osg::Vec3f ValueInterpolator::interpKey(const Nif::Vector3KeyMap::MapType &keys, float time) const
{
    if(time <= keys.begin()->first)
        return keys.begin()->second.mValue;

    Nif::Vector3KeyMap::MapType::const_iterator it = keys.lower_bound(time);
    if (it != keys.end())
    {
        float aTime = it->first;
        const Nif::KeyT<osg::Vec3f>* aKey = &it->second;

        assert (it != keys.begin()); // Shouldn't happen, was checked at beginning of this function

        Nif::Vector3KeyMap::MapType::const_iterator last = --it;
        float aLastTime = last->first;
        const Nif::KeyT<osg::Vec3f>* aLastKey = &last->second;

        float a = (time - aLastTime) / (aTime - aLastTime);
        return aLastKey->mValue + ((aKey->mValue - aLastKey->mValue) * a);
    }
    else
        return keys.rbegin()->second.mValue;
}

ControllerFunction::ControllerFunction(const Nif::Controller *ctrl, bool deltaInput)
    : mDeltaInput(deltaInput)
    , mFrequency(ctrl->frequency)
    , mPhase(ctrl->phase)
    , mStartTime(ctrl->timeStart)
    , mStopTime(ctrl->timeStop)
    , mDeltaCount(0.f)
{
    if(mDeltaInput)
        mDeltaCount = mPhase;
}

float ControllerFunction::calculate(float value)
{
    if(mDeltaInput)
    {
        if (mStopTime - mStartTime == 0.f)
            return 0.f;

        mDeltaCount += value*mFrequency;
        if(mDeltaCount < mStartTime)
            mDeltaCount = mStopTime - std::fmod(mStartTime - mDeltaCount,
                                                mStopTime - mStartTime);
        mDeltaCount = std::fmod(mDeltaCount - mStartTime,
                                mStopTime - mStartTime) + mStartTime;
        return mDeltaCount;
    }

    value = std::min(mStopTime, std::max(mStartTime, value+mPhase));
    return value;
}

osg::Quat KeyframeController::Value::interpKey(const Nif::QuaternionKeyMap::MapType &keys, float time)
{
    if(time <= keys.begin()->first)
        return keys.begin()->second.mValue;

    Nif::QuaternionKeyMap::MapType::const_iterator it = keys.lower_bound(time);
    if (it != keys.end())
    {
        float aTime = it->first;
        const Nif::QuaternionKey* aKey = &it->second;

        assert (it != keys.begin()); // Shouldn't happen, was checked at beginning of this function

        Nif::QuaternionKeyMap::MapType::const_iterator last = --it;
        float aLastTime = last->first;
        const Nif::QuaternionKey* aLastKey = &last->second;

        float a = (time - aLastTime) / (aTime - aLastTime);

        osg::Quat v1 = aLastKey->mValue;
        osg::Quat v2 = aKey->mValue;
        // don't take the long path
        if (v1.x()*v2.x() + v1.y()*v2.y() + v1.z()*v2.z() + v1.w()*v2.w() < 0) // dotProduct(v1,v2)
            v1 = -v1;

        osg::Quat result;
        result.slerp(a, v1, v2);
        return result;
    }
    else
        return keys.rbegin()->second.mValue;
}

osg::Quat KeyframeController::Value::getXYZRotation(float time) const
{
    float xrot = interpKey(mXRotations->mKeys, time);
    float yrot = interpKey(mYRotations->mKeys, time);
    float zrot = interpKey(mZRotations->mKeys, time);
    osg::Quat xr(xrot, osg::Vec3f(1,0,0));
    osg::Quat yr(yrot, osg::Vec3f(0,1,0));
    osg::Quat zr(zrot, osg::Vec3f(0,0,1));
    return (zr*yr*xr);
}

KeyframeController::Value::Value(osg::Node *target, const Nif::NIFFilePtr &nif, const Nif::NiKeyframeData *data,
                                 osg::Quat initialQuat, float initialScale)
    : NodeTargetValue(target)
    , mRotations(&data->mRotations)
    , mXRotations(&data->mXRotations)
    , mYRotations(&data->mYRotations)
    , mZRotations(&data->mZRotations)
    , mTranslations(&data->mTranslations)
    , mScales(&data->mScales)
    , mNif(nif)
    , mInitialQuat(initialQuat)
    , mInitialScale(initialScale)
{ }

osg::Vec3f KeyframeController::Value::getTranslation(float time) const
{
    if(mTranslations->mKeys.size() > 0)
        return interpKey(mTranslations->mKeys, time);
    osg::MatrixTransform* trans = static_cast<osg::MatrixTransform*>(mNode);
    return trans->getMatrix().getTrans();
}

void KeyframeController::Value::setValue(float time)
{
    osg::MatrixTransform* trans = static_cast<osg::MatrixTransform*>(mNode);
    osg::Matrix mat = trans->getMatrix();

    if(mRotations->mKeys.size() > 0)
        mat.setRotate(interpKey(mRotations->mKeys, time));
    else if (!mXRotations->mKeys.empty() || !mYRotations->mKeys.empty() || !mZRotations->mKeys.empty())
        mat.setRotate(getXYZRotation(time));
    else
        mat.setRotate(mInitialQuat);

    // let's hope no one's using multiple KeyframeControllers on the same node (not that would make any sense...)
    float scale = mInitialScale;
    if(mScales->mKeys.size() > 0)
        scale = interpKey(mScales->mKeys, time);

    for (int i=0;i<3;++i)
        for (int j=0;j<3;++j)
            mat(i,j) *= scale;

    if(mTranslations->mKeys.size() > 0)
        mat.setTrans(interpKey(mTranslations->mKeys, time));
    trans->setMatrix(mat);
}

Controller::Controller(boost::shared_ptr<ControllerSource> src, boost::shared_ptr<ControllerValue> dest, boost::shared_ptr<ControllerFunction> function)
    : mSource(src)
    , mDestValue(dest)
    , mFunction(function)
{

}

void Controller::update()
{
    if (mSource.get())
    {
        mDestValue->setValue(mFunction->calculate(mSource->getValue()));
    }
}

GeomMorpherController::Value::Value(osgAnimation::MorphGeometry *geom, const Nif::NiMorphData* morphData)
    : mGeom(geom)
    , mMorphs(morphData->mMorphs)
{

}

void GeomMorpherController::Value::setValue(float time)
{
    if (mMorphs.size() <= 1)
        return;
    int i = 0;
    for (std::vector<Nif::NiMorphData::MorphData>::iterator it = mMorphs.begin()+1; it != mMorphs.end(); ++it,++i)
    {
        float val = 0;
        if (!it->mData.mKeys.empty())
            val = interpKey(it->mData.mKeys, time);
        val = std::max(0.f, std::min(1.f, val));

        mGeom->setWeight(i, val);
    }
}

UVController::Value::Value(osg::StateSet *target, const Nif::NiUVData *data, std::set<int> textureUnits)
    : mStateSet(target)
    , mUTrans(data->mKeyList[0])
    , mVTrans(data->mKeyList[1])
    , mUScale(data->mKeyList[2])
    , mVScale(data->mKeyList[3])
    , mTextureUnits(textureUnits)
{
}

void UVController::Value::setValue(float value)
{
    float uTrans = interpKey(mUTrans.mKeys, value, 0.0f);
    float vTrans = interpKey(mVTrans.mKeys, value, 0.0f);
    float uScale = interpKey(mUScale.mKeys, value, 1.0f);
    float vScale = interpKey(mVScale.mKeys, value, 1.0f);

    osg::Matrixf mat = osg::Matrixf::scale(uScale, vScale, 1);
    mat.setTrans(uTrans, vTrans, 0);

    osg::TexMat* texMat = new osg::TexMat;
    texMat->setMatrix(mat);

    for (std::set<int>::const_iterator it = mTextureUnits.begin(); it != mTextureUnits.end(); ++it)
    {
        mStateSet->setTextureAttributeAndModes(*it, texMat, osg::StateAttribute::ON);
    }
}

bool VisController::Value::calculate(float time) const
{
    if(mData.size() == 0)
        return true;

    for(size_t i = 1;i < mData.size();i++)
    {
        if(mData[i].time > time)
            return mData[i-1].isSet;
    }
    return mData.back().isSet;
}

void VisController::Value::setValue(float time)
{
    bool vis = calculate(time);
    mNode->setNodeMask(vis ? ~0 : 0);
}


}
