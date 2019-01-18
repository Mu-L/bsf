//************************************ bs::framework - Copyright 2018 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Physics/BsSphereCollider.h"
#include "Physics/BsPhysics.h"

namespace bs
{
	SPtr<SphereCollider> SphereCollider::create(float radius, const Vector3& position, const Quaternion& rotation)
	{
		return Physics::instance().createSphereCollider(radius, position, rotation);
	}
}