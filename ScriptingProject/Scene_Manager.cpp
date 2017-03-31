#include "stdafx.h"

#include <string>
#include <map>
#include <vector>
#include "../Application.h"
#include "../ModuleScripting.h"
#include "../ModuleInput.h"
#include "../ModuleWindow.h"
#include "../GameObject.h"
#include "../ComponentScript.h"
#include "../ComponentTransform.h"
#include "../SDL/include/SDL_scancode.h"
#include "../PhysBody3D.h"
#include "../ComponentCollider.h"
#include "../Globals.h"
#include "../RaceTimer.h"
#include "../ComponentCar.h"
#include "../ComponentUiText.h"
#include "../ModuleGOManager.h"

namespace Scene_Manager
{
	RaceTimer timer;
//	GameObject* canvas = nullptr;
	GameObject* car_1_go = nullptr;
	ComponentCar* car_1 = nullptr;

	GameObject* timer_text_go = nullptr;
	ComponentUiText* timer_text = nullptr;

	void Scene_Manager_GetPublics(map<const char*, string>* public_chars, map<const char*, int>* public_ints, map<const char*, float>* public_float, map<const char*, bool>* public_bools, map<const char*, GameObject*>* public_gos)
	{
//		public_gos->insert(std::pair<const char*, GameObject*>("Canvas", canvas));
		public_gos->insert(std::pair<const char*, GameObject*>("Car1", car_1_go));
		public_gos->insert(std::pair<const char*, GameObject*>("Timer_Text", timer_text_go));
	}

	void Scene_Manager_UpdatePublics(GameObject* game_object)
	{

	}

	void Scene_Manager_Start(GameObject* game_object)
	{
		timer.Start();
		if (car_1_go != nullptr)
		{
			car_1 = (ComponentCar*)car_1_go->GetComponent(C_CAR);
		}
		if (timer_text_go != nullptr)
		{
			timer_text = (ComponentUiText*)timer_text_go->GetComponent(C_UI_TEXT);
		}
	}

	void Scene_Manager_Update(GameObject* game_object)
	{
		int min, sec, milisec = 0;
		if (car_1 != nullptr)
		{
			if (car_1->lap + 1 != timer.GetCurrentLap())
				timer.AddLap();

			//Updating UI timer
			if (timer_text != nullptr)
			{
				timer.GetCurrentLapTime(min, sec, milisec);
				string min_str = to_string(min);
				string sec_str = to_string(sec);
				string mil_str = to_string(milisec);
				if (min < 10)
					min_str = "0" + min_str;
				if (sec < 10)
					sec_str = "0" + sec_str;
				if (milisec < 100)
					mil_str = "0" + mil_str;

				string timer_string = min_str + ":" + sec_str + ":" + mil_str;
				timer_text->SetDisplayText(timer_string);
			}
		}
	}
}