#include "stdafx.h"

#include "../Application.h"
#include "../ModuleScripting.h"
#include "../ComponentScript.h"
#include "../ComponentTransform.h"
#include "../ComponentCollider.h"
#include "../ModuleGOManager.h"
#include "../ComponentCar.h"
#include "../GameObject.h"
#include "../PhysBody3D.h"

namespace OutOfBounds
{
	//Public

	void OutOfBounds_GetPublics(map<const char*, string>* public_chars, map<const char*, int>* public_ints, map<const char*, float>* public_float, map<const char*, bool>* public_bools, map<const char*, GameObject*>* public_gos)
	{
	}

	void OutOfBounds_UpdatePublics(GameObject* game_object)
	{
	}

	void OutOfBounds_ActualizePublics(GameObject* game_object)
	{
	}

	void OutOfBounds_Start(GameObject* game_object)
	{}

	void OutOfBounds_Update(GameObject* game_object)
	{}

	void OutOfBounds_OnCollision(GameObject* game_object, PhysBody3D* col)
	{
		ComponentCar* car = col->GetCar();
		ComponentTransform* trs = (ComponentTransform*)game_object->GetComponent(C_TRANSFORM);
		if (car && trs)
		{
			car->Reset();
		}
	}
}