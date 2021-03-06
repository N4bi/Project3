#include "ComponentCar.h"

#include "Application.h"
#include "ModulePhysics3D.h"
#include "ModuleInput.h"
#include "ModuleEditor.h"
#include "ModuleRenderer3D.h"

#include "GameObject.h"
#include "ComponentTransform.h"
#include "ComponentAnimation.h"

#include "imgui/imgui.h"

#include "Primitive.h"
#include "PhysVehicle3D.h"
#include "EventQueue.h"
#include "EventLinkGos.h"

#include "ModuleGOManager.h"
#include "ComponentCanvas.h"

#include "ComponentCollider.h"

#include "Time.h"

#include <string>

#include "SDL\include\SDL_scancode.h"
#include "Brofiler\include\Brofiler.h"

ComponentCar::ComponentCar(GameObject* GO) : Component(C_CAR, GO), chasis_size(1.0f, 0.2f, 2.0f), chasis_offset(0.0f, 0.0f, 0.0f)
{
	SetCarType(T_KOJI);

	car = new VehicleInfo();
	
	car->mass = 400.0f;
	car->suspensionStiffness = 100.0f;
	car->suspensionCompression = 0.83f;
	car->suspensionDamping = 20.0f;
	car->maxSuspensionTravelCm = 1000.0f;
	car->frictionSlip = 50.5;
	car->maxSuspensionForce = 6000.0f;

	car->num_wheels = 4;
	car->wheels = new Wheel[4];

	turn_max = kart->base_turn_max;
	
	//
	reset_pos = { 0.0f, 0.0f, 0.0f };
	reset_rot = { 1.0f, 1.0f, 1.0f, 1.0f};

	for (uint i = 0; i < 4; i++)
		wheels_go.push_back(nullptr);

	//Player config
	front_player = PLAYER_1;
	back_player = PLAYER_2;

	//Turbo
	mini_turbo.SetTurbo("Mini turbo", 300.0f, 25.0f, 1.0f);
	turbos.push_back(mini_turbo);

	drift_turbo_2.SetTurbo("Drift turbo 2", 300.0f, 35.0f, 1.0f);
	turbos.push_back(drift_turbo_2);

	drift_turbo_3.SetTurbo("Drift turbo 3", 300.0f, 45.0f, 2.0f);
	turbos.push_back(drift_turbo_3);

	turbo_pad.SetTurbo("Turbo Pad", 300.0f, 200.0f, 1.5f);
	turbos.push_back(turbo_pad);

	//Item
	rocket_turbo.SetTurbo("Rocket turbo", 0.0f, 50.0f, 5.0f);
	
	inverted_controls = false;
}

ComponentCar::~ComponentCar()
{
	delete car;
}

void ComponentCar::Update()
{
	BROFILER_CATEGORY("ComponentCar::Update", Profiler::Color::GhostWhite)

	if (App->IsGameRunning())
	{
		if (vehicle)
		{		
			CheckGroundCollision();
			HandlePlayerInput();
			if (App->StartInGame() == false)
			{
				vehicle->Render();
			}
			UpdateGO();
			GameLoopCheck();

		}
		else
			CreateCar();
	}
	else
	{
		vehicle = nullptr;
		RenderWithoutCar();
	}

}



void ComponentCar::OnPlay()
{
	if (vehicle == nullptr)
		CreateCar();

	ComponentTransform* trs = (ComponentTransform*)game_object->GetComponent(C_TRANSFORM);
	if (trs)
	{
		reset_pos = trs->GetPosition();
		reset_rot = trs->GetRotation();
	}
	checkpoints = MAXUINT - 10;
	lap = 1;
	raceStarted = false;
	finished = false;
	n_checkpoints = 0;
}

void ComponentCar::SetFrontPlayer(PLAYER player)
{
	if (player >= 0 && player < 4)
	{
		front_player = player;
	}
}

void ComponentCar::SetBackPlayer(PLAYER player)
{
	if (player >= 0 && player < 4)
	{
		back_player = player;
	}
}

void ComponentCar::BlockInput(bool block)
{
	lock_input = block;
}

void ComponentCar::TestFunction()
{
	LOG("Test function");
	return;
}

float ComponentCar::GetVelocity() const
{
	return vehicle->GetKmh();
}

void ComponentCar::HandlePlayerInput()
{
	BROFILER_CATEGORY("ComponentCar::HandlePlayerInput", Profiler::Color::HoneyDew)
	turn_max = GetMaxTurnByCurrentVelocity(GetVelocity());

	float brake;
	bool turning = false;
	leaning = false;
	accel_boost = speed_boost = turn_boost = 0.0f;
	
	accel = brake = 0.0f;

	if (pushing)
	{
		PushUpdate(&accel_boost);
	}

	if (drifting == true)
	{
		turn_max = kart->drift_turn_max;
	}
	
	if (lock_input == false)
	{
		KeyboardControls(&accel, &brake, &turning, inverted_controls);

		JoystickControls(&accel, &brake, &turning, inverted_controls);
	}

	ApplyTurbo();

	//Acrobactics control
	if (acro_on)
	{
		acro_timer += time->DeltaTime();

		if (acro_timer >= kart->acro_time)
		{
			acro_on = false;
			acro_timer = 0.0f;
			acro_back = false;
			acro_front = false;
		}
	}
	
	//---------------------
	LimitTurn();

	if (!turning)
		IdleTurn();

	if (drifting )
		CalcDriftForces();

	if (p2_animation != nullptr && p2_animation->current_animation != nullptr)
	{
		if (p2_animation->current_animation->index == 5)
		{
			//PushUpdate(&accel);
		}
	}

	if (vehicle)
	{
		accel += accel_boost;
		//Doing this so it doesn't stop from braking
		vehicle->Turn(turn_current);
		vehicle->ApplyEngineForce(accel);
		vehicle->Brake(brake);

		if (!accel && !brake)
		{
			vehicle->Brake(kart->decel_brake);
		}

		LimitSpeed();
	}



	UpdateTurnOver();
}

void ComponentCar::JoystickControls(float* accel, float* brake, bool* turning, bool inverse)
{
	bool acro_front, acro_back;
	acro_front = acro_back = false;

	if (App->input->GetNumberJoysticks() > 0)
	{

		//Handling
		PLAYER trn_player = front_player;
		if (drifting)
			trn_player = back_player;

		float x_joy_input = App->input->GetJoystickAxis(trn_player, JOY_AXIS::LEFT_STICK_X);
		*turning = JoystickTurn(&turning_left, x_joy_input);

		if (App->input->GetJoystickButton(trn_player, JOY_BUTTON::DPAD_RIGHT) == KEY_REPEAT)
		{
			inverse ? *turning = Turn(&turning_left, false) : *turning = Turn(&turning_left, true);
		}
		if (App->input->GetJoystickButton(trn_player, JOY_BUTTON::DPAD_LEFT) == KEY_REPEAT)
		{
			inverse ? *turning = Turn(&turning_left, true) : *turning = Turn(&turning_left, false);
		}


		//Y back
		if (App->input->GetJoystickButton(back_player, JOY_BUTTON::Y) == KEY_REPEAT)
		{

		}

		//X front
		if (App->input->GetJoystickButton(front_player, JOY_BUTTON::X) == KEY_DOWN)
		{
			if (on_ground && *turning == true)
				StartDrift();
			else if (on_ground == false)
				Acrobatics(front_player);
		}
		else if (drifting == true && App->input->GetJoystickButton(front_player, JOY_BUTTON::X) == KEY_UP)
		{
			EndDrift();
		}

		//X back
		if (App->input->GetJoystickButton(back_player, JOY_BUTTON::X) == KEY_DOWN)
		{
			Acrobatics(back_player);
		}

		//A back
		if (App->input->GetJoystickButton(back_player, JOY_BUTTON::A) == KEY_DOWN)
		{
			if (drifting)
				DriftTurbo();
			else
				Push(accel);
		}

		//Front RT
		if (App->input->GetJoystickAxis(front_player, JOY_AXIS::RIGHT_TRIGGER))
		{
			float rt_joy_axis = App->input->GetJoystickAxis(front_player, JOY_AXIS::RIGHT_TRIGGER);
			Accelerate(accel, true, rt_joy_axis);
		}

		//Front LT
		if (App->input->GetJoystickAxis(front_player, JOY_AXIS::LEFT_TRIGGER))
		{
			float lt_joy_axis = App->input->GetJoystickAxis(front_player, JOY_AXIS::LEFT_TRIGGER);
			Brake(accel, brake, true, lt_joy_axis);
		}


		//Power Up
		/*if (App->input->GetJoystickButton(back_player, JOY_BUTTON::B) == KEY_REPEAT)
		{
		UseItem();
		}

		if (App->input->GetJoystickButton(back_player, JOY_BUTTON::B) == KEY_UP)
		{
		ReleaseItem();
		}*/

	}
}

void ComponentCar::KeyboardControls(float* accel, float* brake, bool* turning, bool inverse)
{
	//Back player
	if (App->input->GetKey(SDL_SCANCODE_K) == KEY_DOWN)
	{
		//StartPush();
		if (drifting)
			DriftTurbo();
		else
			Push(accel);
	}
	if (App->input->GetKey(SDL_SCANCODE_J) == KEY_REPEAT)
	{
		//Leaning(*accel);
	}
	if (App->input->GetKey(SDL_SCANCODE_L) == KEY_DOWN)
	{
		OnGetHit();
		//Acrobatics(back_player);
	}
	/*if (App->input->GetKey(SDL_SCANCODE_Q) == KEY_REPEAT)
	{
		//current_turbo = T_MINI;
		UseItem();
	}
	if (App->input->GetKey(SDL_SCANCODE_Q) == KEY_UP)
	{
		//current_turbo = T_MINI;
		ReleaseItem();
	}*/

		if (App->input->GetKey(SDL_SCANCODE_X) == KEY_REPEAT)
		{
			FullBrake(brake);
		}


		//Front player
		if (App->input->GetKey(front_player == PLAYER_1 ? SDL_SCANCODE_W : SDL_SCANCODE_UP) == KEY_REPEAT)
		{
			Accelerate(accel);
		}
		if (App->input->GetKey(front_player == PLAYER_1 ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT) == KEY_REPEAT)
		{
			*turning = Turn(&turning_left, inverse);
		}
		if (App->input->GetKey(front_player == PLAYER_1 ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT) == KEY_REPEAT)
		{
			*turning = Turn(&turning_left, !inverse);
		}
		if (App->input->GetKey(front_player == PLAYER_1 ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN) == KEY_REPEAT)
		{
			Brake(accel, brake);
		}
		if (App->input->GetKey(SDL_SCANCODE_R) == KEY_DOWN)
		{
			Reset();
		}
		if (App->input->GetKey(SDL_SCANCODE_E) == KEY_DOWN)
		{
			//Acrobatics(back_player);
		}

		if (App->input->GetKey(SDL_SCANCODE_SPACE) == KEY_DOWN && *turning == true)
		{
			if (on_ground && *turning == true)
				StartDrift();
			else if(on_ground == false)
				Acrobatics(front_player);
		}
		else if (App->input->GetKey(SDL_SCANCODE_SPACE) == KEY_UP && drifting == true)
		{
			EndDrift();
		}
	}
// CONTROLS-----------------------------
bool ComponentCar::Turn(bool* left_turn, bool left)
{
	bool ret = true;
	float t_speed = kart->turn_speed;

	float top_turn = turn_max + turn_boost;

	if (drifting == false)
	{
		if (left)
		{
			*left_turn = true;
		}
		else
		{
			*left_turn = false;
			t_speed = -t_speed;
		}
	}
	else if (left == false)
		t_speed = -t_speed;

	turn_current += t_speed * time->DeltaTime();

	if (drifting == false)
	{
		if (turn_current > top_turn)
			turn_current = top_turn;

		else if(turn_current < -top_turn)
			turn_current = -top_turn;
	}
	else
	{
		//Drifting wheel limitation 0 -> top_current
		if (drift_dir_left == false)
			top_turn = -top_turn;
		if (drift_dir_left ? turn_current < 0 : turn_current > 0)
			turn_current = 0;
		if (drift_dir_left ? turn_current > top_turn : turn_current < top_turn)
			turn_current = top_turn;
	}
	return true;
}

bool ComponentCar::JoystickTurn(bool* left_turn, float x_joy_input)
{
	float top_turn = turn_max + turn_boost;

	if (math::Abs(x_joy_input) > 0.2f)
	{
		x_joy_input < 0.0f ? *left_turn = true : *left_turn = false;

		if (drifting == false)
		{
			turn_current += (kart->turn_speed_joystick * -x_joy_input) * time->DeltaTime();

		}
		else
		{
			//Normalizing x_joy_input to 0-1 vlau

			if (drift_dir_left == true)
			{
				turn_current = -top_turn * x_joy_input;
			}
			else
			{
				turn_current = -top_turn * x_joy_input;
			}

			
			//Turn limitation
			if (drift_dir_left == false)
				top_turn = -top_turn;
			if (drift_dir_left ? turn_current < 0 : turn_current > 0)
				turn_current = 0;
			if (drift_dir_left ? turn_current > top_turn : turn_current < top_turn)
				turn_current = top_turn;
		}
		return true;
	}
	return false;
}

void ComponentCar::LimitTurn()
{
	float top_turn = turn_max + turn_boost;

	if (turn_current > top_turn)
		turn_current = top_turn;

	else if (turn_current < -top_turn)
		turn_current = -top_turn;
}

void ComponentCar::Brake(float* accel, float* brake, bool with_trigger, float lt_joy_axis)
{
	float ba_force = kart->back_force;
	float br_force = kart->brake_force;

	if (with_trigger)
	{
		lt_joy_axis++;
		lt_joy_axis /= 2;
		if (math::Abs(lt_joy_axis) > 0.2f)
		{
			ba_force *= lt_joy_axis;
			br_force *= lt_joy_axis;

			if (vehicle->GetKmh() <= 0)
				*accel = -ba_force;

			else
				*brake = br_force;
		}

	}

	else
	{
		if (vehicle->GetKmh() <= 0)
			*accel = -ba_force;

		else
			*brake = br_force;
	}
}

void ComponentCar::FullBrake(float* brake)
{
	if (vehicle->GetKmh() > 0)
		*brake = kart->full_brake_force;
}
void ComponentCar::Accelerate(float* accel, bool with_trigger, float rt_joy_axis)
{
	if (with_trigger)
	{
		rt_joy_axis++;
		rt_joy_axis /= 2;

		if (math::Abs(rt_joy_axis) > 0.2f)
		{
			*accel += kart->accel_force * rt_joy_axis;
		}
	}
	else
	{
		*accel += kart->accel_force;
	}
}

void ComponentCar::StartPush()
{
	pushing = true;
	pushStartTime = time->TimeSinceGameStartup();
}

bool ComponentCar::Push(float* accel)
{
	bool ret = false;
	if (vehicle->GetKmh() < (kart->max_velocity / 100)* kart->push_speed_per)
	{
		pushing = true;
	}
	pushStartTime = time->TimeSinceGameStartup();

	return ret;
}

void ComponentCar::PushUpdate(float* accel)
{
	if (time->TimeSinceGameStartup() - pushStartTime >= 0.5f)
		pushing = false;

	if (pushing)
	{
		*accel += kart->push_force;
	}
}

void ComponentCar::Leaning(float accel)
{
	if (vehicle->GetKmh() > 0.0f && current_turbo == T_IDLE)
	{
		SetP2AnimationState(P2LEANING, 0.5f);
		leaning = true;
		/*accel_boost += ((accel / 100)*lean_top_acc);
		speed_boost += ((max_velocity / 100)*lean_top_sp);
		turn_boost -= ((turn_max / 100)*lean_red_turn);*/
	}
}

void ComponentCar::Acrobatics(PLAYER p)
{
	if (!on_ground)
	{
		bool tmp_front = acro_front;
		bool tmp_back = acro_back;

		if (p == front_player)
		{
			acro_front = true;
		}
		else if (p == back_player)
		{
			acro_back = true;
		}


		if (acro_back && acro_front)
		{
			//Applieds for the drifting turbo at VS3
			//Apply turbo
			//current_turbo = T_MINI;
			/*
			if (drifting)
			{
				switch (turbo_drift_lvl)
				{
				case 0:
					turbo_drift_lvl = 1;
					break;
				case 1:
					turbo_drift_lvl = 2;
					break;
				case 2:
					turbo_drift_lvl = 3;
					break;
				}

				to_drift_turbo = true;
			}*/
			//Normal acrobatics
			acro_done = true;

			acro_front = false;
			acro_back = false;
		}



		else if (tmp_back != acro_back || tmp_front != acro_front)
		{
			//Start timer
			acro_timer = 0.0f;

			acro_on = true;
			SetP2AnimationState(P2ACROBATICS, 0.5f);
			p1_state = P1ACROBATICS;
			p1_animation->PlayAnimation(4, 0.5f);
		}
	}
}

void ComponentCar::PickItem()
{
	has_item = true;
}

void ComponentCar::UseItem()
{
	SetP2AnimationState(P2USE_ITEM, 0.5f);

	//if (has_item)
	//{
		current_turbo = T_ROCKET;
		has_item = false;
	//}

	if (applied_turbo && current_turbo)
	{
		if (applied_turbo->timer >= applied_turbo->time)
		{
			ReleaseItem();
			vehicle->SetLinearSpeed(0.0f, 0.0f, 0.0f);
			current_turbo = T_IDLE;
		}
	}
}

void ComponentCar::ReleaseItem()
{
	if (current_turbo = T_ROCKET)
	{
		current_turbo = T_IDLE;
	}
}
bool ComponentCar::AddHitodama()
{
	if (num_hitodamas < max_hitodamas)
	{
		num_hitodamas++;
		return true;
	}
	return false;
}
bool ComponentCar::RemoveHitodama()
{
	if (num_hitodamas > 0)
	{
		num_hitodamas--;
		return true;
	}
	return false;
}
int ComponentCar::GetNumHitodamas() const
{
	return num_hitodamas;
}
void ComponentCar::IdleTurn()
{
	//By turn interpolation
	float  t_idle_speed = kart->turn_speed;

	if(kart->idle_turn_by_interpolation)
		 t_idle_speed = turn_max / kart->time_to_idle;

	//By turn speed
	if (turn_current > 0)
	{
		turn_current -= t_idle_speed * time->DeltaTime();
		if (turn_current < 0)
			turn_current = 0;
	}
	else if (turn_current < 0)
	{
		turn_current += t_idle_speed * time->DeltaTime();
		if (turn_current > 0)
			turn_current = 0;
	}
}

void ComponentCar::ApplyTurbo()
{
	bool start = false;

	if (start = (last_turbo != current_turbo))
	{
		switch (current_turbo)
		{
		case T_IDLE:
			applied_turbo = nullptr;
			break;
		case T_MINI:
			applied_turbo = &mini_turbo;
			break;
		case T_DRIFT_MACH_2:
			applied_turbo = &drift_turbo_2;
			break;
		case T_DRIFT_MACH_3:
			applied_turbo = &drift_turbo_3;
			break;
		case T_TURBOPAD:
			applied_turbo = &turbo_pad;
			break;
		case T_ROCKET:
			applied_turbo = &rocket_turbo;
			break;
		}
	}

	last_turbo = current_turbo;

	//If there's a turbo on, apply it
	if (applied_turbo)
	{
	

		//Changes applied when turbo started
		if (start)
		{
			applied_turbo->timer = 0.0f;

			if (applied_turbo->per_ac)
				turbo_accel_boost = ((kart->accel_force / 100) * applied_turbo->accel_boost);
			else
				turbo_accel_boost = applied_turbo->accel_boost;

			if (applied_turbo->per_sp)
				turbo_speed_boost = ((kart->max_velocity / 100) * applied_turbo->speed_boost);
			else
				turbo_speed_boost = applied_turbo->speed_boost;


			if (applied_turbo->speed_direct && !applied_turbo->speed_increase)
			{
				float3 fv = game_object->transform->GetForward();
				float s_offset = 0.5;
				vehicle->SetVelocity(fv.x, fv.y, fv.z, kart->max_velocity + turbo_speed_boost - s_offset);
			}

			turbo_deceleration = applied_turbo->deceleration;
			turbo_acceleration = applied_turbo->fake_accel;
			to_turbo_decelerate = applied_turbo->speed_decrease;
			current_speed_boost = 0.0f;
			speed_boost_reached = false;

		}

		//Turbo applied every frame till it's time finish and then go to idle turbo
		if (applied_turbo->timer < applied_turbo->time)
		{
			if (!speed_boost_reached)
			{
				if (applied_turbo->speed_direct && applied_turbo->speed_increase)
				{
					//Testing inn progress of progressive acceleration
					current_speed_boost += turbo_acceleration * time->DeltaTime();

					if (vehicle->GetKmh() > top_velocity)
					{
						speed_boost_reached = true;
					}

					float3 fv = game_object->transform->GetForward();
					float s_offset = 0.5;
					float current_velocity = GetVelocity();
					float desired_velocity = GetVelocity() + turbo_acceleration; //* time->DeltaTime();
					vehicle->SetVelocity(fv.x, fv.y, fv.z, desired_velocity);
				}

			}
			

			accel_boost += turbo_accel_boost;
			speed_boost += turbo_speed_boost;

			applied_turbo->timer += time->DeltaTime();
		}
		else
		{
			current_turbo = T_IDLE;
		}
	}

	//Deceleration (without brake)
	if (current_turbo == T_IDLE && to_turbo_decelerate)
	{
		if (turbo_speed_boost > 0.0f)
		{
			turbo_speed_boost -= turbo_deceleration * time->DeltaTime();
			speed_boost += turbo_speed_boost;
		}
	}
}

void ComponentCar::StartDrift()
{
	
	if (GetVelocity() >= kart->drift_min_speed)
	{
		drifting = true;
		drift_dir_left = turning_left;
		startDriftSpeed = vehicle->vehicle->getRigidBody()->getLinearVelocity();
		vehicle->SetFriction(0);
	}
	
}

void ComponentCar::CalcDriftForces()
{
	if (on_ground)
	{

		vehicle->vehicle->getRigidBody()->clearForces();

		float4x4 matrix;
		vehicle->GetRealTransform().getOpenGLMatrix(matrix.ptr());
		matrix.Transpose();

		float3 front = matrix.WorldZ();
		float3 left = matrix.WorldX();
		float3 final_dir;
		if (drift_dir_left == true)
			left = -left;


		final_dir = left.Lerp(front, kart->drift_ratio);

		btVector3 vector(final_dir.x, final_dir.y, final_dir.z);
		float l = startDriftSpeed.length();
		btVector3 final_vector = vector * l * kart->drift_mult;
		btVector3 zero = { 0,0,0 };

		final_vector.setY(0.0f);
		vehicle->vehicle->getRigidBody()->setLinearVelocity(vector * l * kart->drift_mult);
	}
	else
	{
		EndDrift();
	}



		//Debugging lines
		//Front vector
		/*float3 start_line = matrix.TranslatePart();
		float3 end_line = start_line + front;
		App->renderer3D->DrawLine(start_line, end_line, float4(1, 0, 0, 1));
		//Left vector
		end_line = start_line + left;
		App->renderer3D->DrawLine(start_line, end_line, float4(0, 1, 0, 1));
		//Force vector
		end_line = start_line + final_dir;
		App->renderer3D->DrawLine(start_line, end_line, float4(1, 1, 1, 1));*/

}

void ComponentCar::EndDrift()
{
	vehicle->Turn(0);
	turn_current = 0;
	vehicle->SetFriction(car->frictionSlip);
	float4x4 matrix;
	vehicle->GetRealTransform().getOpenGLMatrix(matrix.ptr());
	matrix.Transpose();

	float3 out_vector = matrix.WorldZ() * (float)startDriftSpeed.length();
	vehicle->vehicle->getRigidBody()->setLinearVelocity(btVector3(out_vector.x, out_vector.y, out_vector.z));
	//vehicle->SetLinearSpeed(startDriftSpeed);
	drifting = false;

	//New turbo
	if (to_drift_turbo && on_ground)
	{
		switch (turbo_drift_lvl)
		{
		case 0:
			break;
		case 1:
			current_turbo = T_MINI;
			break;
		case 2:
			current_turbo = T_DRIFT_MACH_2;
			break;
		case 3:
			current_turbo = T_DRIFT_MACH_3;
			break;

		}

		turbo_drift_lvl = 0;
		to_drift_turbo = false;
	}
}

void ComponentCar::DriftTurbo()
{
	if (drifting)
	{
		drift_turbo_clicks++;

		if (drift_turbo_clicks >= clicks_to_drift_turbo)
		{
			drift_turbo_clicks = 0;
			to_drift_turbo = true;

			if (turbo_drift_lvl < 3)
				turbo_drift_lvl++;
		}
	}
}

void ComponentCar::UpdateTurnOver()
{
	float4x4 matrix;
	vehicle->GetRealTransform().getOpenGLMatrix(matrix.ptr());
	float3 up_vector = matrix.WorldY();

	if (up_vector.y < 0 && turned == false)
	{
		turned = true;
	}
	else if (turned = true)
	{
		if (up_vector.y < 0)
		{
			timer_start_turned += time->DeltaTime();
		}
		else if (up_vector.y > 0)
		{
			turned = false;
			timer_start_turned = 0.0f;
		}
		
		if (timer_start_turned >= turn_over_reset_time)
		{
			TurnOver();
			timer_start_turned = 0.0f;
			turned = false;
		}
	}
		
}

void ComponentCar::SetP2AnimationState(Player2_State state, float blend_ratio)
{
	switch (state)
	{
		case (P2IDLE):
		{
			p2_state = state;
			p2_animation->PlayAnimation(3, blend_ratio);
			break;
		}
		case(P2DRIFT_LEFT):
		{
			p2_state = state;
			p2_animation->PlayAnimation(2, blend_ratio);
			break;
		}
		case(P2DRIFT_RIGHT):
		{
			p2_state = state;
			p2_animation->PlayAnimation(1, blend_ratio);
			break;
		}
		case(P2PUSH_START):
		{
			p2_state = state;
			p2_animation->PlayAnimation(4, blend_ratio);
			break;
		}
		case(P2PUSH_LOOP):
		{
			p2_state = state;
			p2_animation->PlayAnimation(5, blend_ratio);
			break;
		}
		case(P2PUSH_END):
		{
			p2_state = state;
			p2_animation->PlayAnimation(6, blend_ratio);
			break;
		}
		case(P2LEANING):
		{
			if (p2_state != P2LEANING)
			{
				p2_state = state;
				p2_animation->PlayAnimation(7, blend_ratio);
			}
			break;
		}
		case (P2GET_HIT):
		{
			p2_state = state;
			p2_animation->PlayAnimation(8, blend_ratio);
			break;
		}
		case(P2USE_ITEM):
		{
			p2_state = state;
			p2_animation->PlayAnimation(9, blend_ratio);
			break;
		}
		case(P2ACROBATICS):
		{
			p2_state = state;
			p2_animation->PlayAnimation(10, blend_ratio);
			break;
		}
	}
}

void ComponentCar::UpdateP1Animation()
{
	switch (p1_state)
	{
		case(P1ACROBATICS):
		{
			if (p1_animation->playing == false)
			{
				p1_state = P1IDLE;
			}
			break;
		}
		case(P1GET_HIT):
		{
			if (p1_animation->playing == false)
			{
				p1_state = P1IDLE;
			}
			break;
		}
		case(P1MAXTURN_L):
		{
			if (turn_current < turn_max + turn_boost)
				p1_state = P1IDLE;
			break;
		}
		case (P1MAXTURN_R):
		{
			if (turn_current > - turn_max - turn_boost)
				p1_state = P1IDLE;
			break;
		}
		case(P1TURN):
		{
			float ratio = (-turn_current + turn_max + turn_boost) / (turn_max + turn_boost + (turn_max + turn_boost));
			p1_animation->LockAnimationRatio(ratio);
			if (turn_current >= turn_max + turn_boost || turn_current <= -turn_max - turn_boost) p1_state = P1IDLE;
			break;
		}
		case(P1IDLE):
		{
			if (turn_current >= turn_max + turn_boost)
			{
				p1_state = P1MAXTURN_L;
				p1_animation->PlayAnimation(1, 0.5f);
			}
			else if (turn_current <= -turn_max - turn_boost)
			{
				p1_state = P1MAXTURN_R;
				p1_animation->PlayAnimation(2, 0.5f);
			}
			else
			{
				p1_state = P1TURN;
				p1_animation->PlayAnimation((uint)0, 0.5f);
				float ratio = (-turn_current + turn_max + turn_boost) / (turn_max + turn_boost + (turn_max + turn_boost));
				p1_animation->LockAnimationRatio(ratio);
			}
			break;
		}
	}
}

void ComponentCar::UpdateP2Animation()
{
	switch (p2_state)
	{
		case(P2IDLE):
		{
			if (drifting == true)
			{
				SetP2AnimationState(drift_dir_left ? P2DRIFT_LEFT : P2DRIFT_RIGHT);
			}
			else if (pushing == true)
			{
				SetP2AnimationState(P2PUSH_START);
			}
			else
			{
				if (p2_animation->current_animation->index != 3) SetP2AnimationState(P2IDLE);
				p2_animation->current_animation->ticks_per_second = 8.0f + 24.0f * (GetVelocity() / (kart->max_velocity + speed_boost));
			}
			break;
		}
		case(P2PUSH_START):
		{
			if (p2_animation->playing == false && pushing == true)
			{
				SetP2AnimationState(P2PUSH_LOOP);
			}
			else if (p2_animation->playing == false)
			{
				SetP2AnimationState(P2PUSH_END);
			}
			break;
		}
		case(P2PUSH_LOOP):
		{
			if (pushing == false)
			{
				SetP2AnimationState(P2PUSH_END);
			}
			break;
		}
		case (P2PUSH_END):
		{
			if (p2_animation->playing == false)
			{
				SetP2AnimationState(P2IDLE);
			}
			break;
		}
		case(P2DRIFT_LEFT):
		{
			if (drifting == false)
			{
				SetP2AnimationState(P2IDLE);
			}
			break;
		}
		case(P2DRIFT_RIGHT):
		{
			if (drifting == false)
			{
				SetP2AnimationState(P2IDLE);
			}
			break;
		}
		case(P2LEANING):
		{
			if (leaning == false)
			{
				SetP2AnimationState(P2IDLE);
			}
			break;
		}
		case(P2GET_HIT):
		{
			if (p2_animation->playing == false)
			{
				SetP2AnimationState(P2IDLE, 0.0f);
			}
			break;
		}
		case(P2ACROBATICS):
		{
			if (p2_animation->playing == false)
			{
				SetP2AnimationState(P2IDLE);
			}
			break;
		}
		case(P2USE_ITEM):
		{
			if (p2_animation->playing == false)
			{
				SetP2AnimationState(P2IDLE);
			}
			break;
		}
	}
}

void ComponentCar::OnGetHit()
{
	GetVehicle()->SetLinearSpeed(0.0f, 0.0f, 0.0f);
	SetP2AnimationState(P2GET_HIT, 0.0f);
	p1_state = P1GET_HIT;
	p1_animation->PlayAnimation(3, 0.5f);
}

void ComponentCar::WentThroughCheckpoint(int checkpoint, float3 resetPos, Quat resetRot)
{
	if (checkpoint == checkpoints + 1)
	{
		n_checkpoints++;
		last_check_pos = resetPos;
		last_check_rot = resetRot;
		checkpoints = checkpoint;
	}
}

void ComponentCar::WentThroughEnd(int checkpoint, float3 resetPos, Quat resetRot)
{
	if (checkpoints + 1 >= checkpoint)
	{
		if (raceStarted == false)
		{
			raceStarted = true;
		}
		else
		{
			lap++;
		}
		n_checkpoints++;
		checkpoints = 0;
		last_check_pos = resetPos;
		last_check_rot = resetRot;
		
	}
	if (lap >= 4)
	{
		finished = true;
		BlockInput(true);
	}
}
//--------------------------------------

void ComponentCar::GameLoopCheck()
{
	BROFILER_CATEGORY("ComponentCar::GameLoopCheck", Profiler::Color::HoneyDew)
	if (game_object->transform->GetPosition().y <= lose_height)
		TurnOver();
}

void ComponentCar::TurnOver()
{
	Reset();
	/*float3 current_pos = vehicle->GetPos();
	current_pos.y += 2;
	float4x4 matrix = float4x4::identity;
	matrix.Translate(current_pos);
	vehicle->SetTransform(matrix.ptr());*/
}

void ComponentCar::Reset()
{
	if (checkpoints >= MAXUINT - 20)
	{
		vehicle->SetPos(reset_pos.x, reset_pos.y, reset_pos.z);
		vehicle->SetRotation(reset_rot.x, reset_rot.y, reset_rot.z);
	}
	else
	{
		vehicle->SetPos(last_check_pos.x, last_check_pos.y, last_check_pos.z);
		vehicle->SetRotation(last_check_rot);
	}
	vehicle->SetLinearSpeed(0.0f, 0.0f, 0.0f);
	vehicle->SetAngularSpeed(0.0f, 0.0f, 0.0f);
}

void ComponentCar::LimitSpeed()
{
	//Tmp convertor
	float KmhToMs = 0.277;

	if (vehicle)
	{
		top_velocity = kart->max_velocity + speed_boost + (num_hitodamas*bonus_hitodamas);
		//Here went definition of top_velocity
		if (GetVelocity() > top_velocity)
		{
			vehicle->SetModularSpeed(top_velocity * KmhToMs);
		}
		else if (GetVelocity() < kart->min_velocity)
		{
			vehicle->SetModularSpeed(-(kart->min_velocity * KmhToMs));
		}
	}
}

float ComponentCar::GetVelocity()
{
	return vehicle->GetKmh();
}

float ComponentCar::GetMaxVelocity() const
{
	return kart->max_velocity;
}

void ComponentCar::SetMaxVelocity(float max_vel)
{
	kart->max_velocity = max_vel;
}

float ComponentCar::GetMinVelocity() const
{
	return kart->min_velocity;
}

float ComponentCar::GetMaxTurnByCurrentVelocity(float sp)
{
	float max_t = kart->base_turn_max;


	if (sp <= kart->velocity_to_begin_change)
	{
		return max_t;
	}
	else
	{
		if (current_max_turn_change_mode == M_SPEED)
		{
			float velocity_dif = sp - kart->velocity_to_begin_change;
			
			max_t += (velocity_dif * kart->base_max_turn_change_speed);

			if (accelerated_change)
			{
				max_t += ((kart->base_max_turn_change_accel / 2) * velocity_dif * velocity_dif);
			}

			if (max_t < kart->turn_max_limit)
			{
				max_t = kart->turn_max_limit;
			}

		}

		else if (current_max_turn_change_mode == M_INTERPOLATION)
		{
			float turn_max_change_dif = kart->turn_max_limit - kart->base_turn_max;
			float velocity_dif = kart->max_velocity - kart->velocity_to_begin_change;

			max_t += (turn_max_change_dif / velocity_dif) * (sp - kart->velocity_to_begin_change);
		}

		
	}


	return max_t;
}

unsigned int ComponentCar::GetFrontPlayer()
{
	return front_player;
}

unsigned int ComponentCar::GetBackPlayer()
{
	return back_player;
}

PhysVehicle3D* ComponentCar::GetVehicle()
{
	return vehicle;
}

bool ComponentCar::GetGroundState() const
{
	return on_ground;
}

float ComponentCar::GetAngularVelocity() const
{
	if (vehicle->vehicle->getRigidBody() != nullptr)
	{
		return vehicle->vehicle->getRigidBody()->getAngularVelocity().length();
	}
}

TURBO ComponentCar::GetCurrentTurbo() const
{
	return current_turbo;
}

Turbo* ComponentCar::GetAppliedTurbo() const
{
	return applied_turbo;
}

void ComponentCar::SetCarType(CAR_TYPE type)
{
	switch (type)
	{
	case T_KOJI:
		kart = &koji;
		kart->type = T_KOJI;
		break;
	case T_WOOD:
		kart = &wood;
		kart->type = T_WOOD;
		break;
	}
}

void ComponentCar::CheckGroundCollision()
{
	BROFILER_CATEGORY("ComponentCar::CheckGroundCollision", Profiler::Color::HoneyDew)
	bool last_contact = on_ground;

	on_ground = vehicle->IsVehicleInContact();

	if (on_ground != last_contact)
	{
		if (last_contact)
			OnGroundCollision(G_EXIT);
		else
			OnGroundCollision(G_BEGIN);
	}

	//We don't need repeat nor none for now

}

void ComponentCar::OnGroundCollision(GROUND_CONTACT state)
{
	if (state == G_EXIT)
	{
		//Changes when exits ground contact
	}
	else if (state == G_BEGIN)
	{
		if (acro_done)
		{
			current_turbo = T_MINI;
			acro_done = false;
		}
		else if (acro_on)
			acro_on = false;
		//Changes when entres ground contact
	}
}
void ComponentCar::CreateCar()
{
	car->transform.Set(game_object->transform->GetGlobalMatrix());

	// Car properties ----------------------------------------
	car->chassis_size.Set(chasis_size.x, chasis_size.y, chasis_size.z);
	car->chassis_offset.Set(chasis_offset.x, chasis_offset.y, chasis_offset.z);


	float half_width = car->chassis_size.x*0.5f;
	float half_length = car->chassis_size.z*0.5f;

	float3 direction(0, -1, 0);
	float3 axis(-1, 0, 0);

	// FRONT-LEFT ------------------------
	car->wheels[0].connection.Set(half_width - 0.1f * wheel_width + chasis_offset.x, connection_height + chasis_offset.y, half_length - wheel_radius + chasis_offset.z);
	car->wheels[0].direction = direction;
	car->wheels[0].axis = axis;
	car->wheels[0].suspensionRestLength = suspensionRestLength;
	car->wheels[0].radius = wheel_radius;
	car->wheels[0].width = wheel_width;
	car->wheels[0].front = true;
	car->wheels[0].drive = false;
	car->wheels[0].brake = false;
	car->wheels[0].steering = true;

	// FRONT-RIGHT ------------------------
	car->wheels[1].connection.Set(-half_width + 0.1 * wheel_width + chasis_offset.x, connection_height + chasis_offset.y, half_length - wheel_radius + chasis_offset.z);
	car->wheels[1].direction = direction;
	car->wheels[1].axis = axis;
	car->wheels[1].suspensionRestLength = suspensionRestLength;
	car->wheels[1].radius = wheel_radius;
	car->wheels[1].width = wheel_width;
	car->wheels[1].front = true;
	car->wheels[1].drive = false;
	car->wheels[1].brake = false;
	car->wheels[1].steering = true;

	// REAR-LEFT ------------------------
	car->wheels[2].connection.Set(half_width - 0.1f * wheel_width + chasis_offset.x, connection_height + chasis_offset.y, -half_length + wheel_radius + chasis_offset.z);
	car->wheels[2].direction = direction;
	car->wheels[2].axis = axis;
	car->wheels[2].suspensionRestLength = suspensionRestLength;
	car->wheels[2].radius = wheel_radius;
	car->wheels[2].width = wheel_width;
	car->wheels[2].front = false;
	car->wheels[2].drive = true;
	car->wheels[2].brake = true;
	car->wheels[2].steering = false;

	// REAR-RIGHT ------------------------
	car->wheels[3].connection.Set(-half_width + 0.1f * wheel_width + chasis_offset.x, connection_height + chasis_offset.y, -half_length + wheel_radius + chasis_offset.z);
	car->wheels[3].direction = direction;
	car->wheels[3].axis = axis;
	car->wheels[3].suspensionRestLength = suspensionRestLength;
	car->wheels[3].radius = wheel_radius;
	car->wheels[3].width = wheel_width;
	car->wheels[3].front = false;
	car->wheels[3].drive = true;
	car->wheels[3].brake = true;
	car->wheels[3].steering = false;

	vehicle = App->physics->AddVehicle(*car, this);
}

void ComponentCar::OnTransformModified()
{}

void ComponentCar::UpdateGO()
{
	BROFILER_CATEGORY("ComponentCar::UpdateGO", Profiler::Color::HoneyDew)
	BROFILER_CATEGORY("ComponentCar::UpdateGO", Profiler::Color::DarkBlue);
	game_object->transform->Set(vehicle->GetTransform().Transposed());
	/*
	for (uint i = 0; i < wheels_go.size(); i++)
	{
		if (wheels_go[i] != nullptr)
		{
			ComponentTransform* w_trs = (ComponentTransform*)wheels_go[i]->GetComponent(C_TRANSFORM);
			float4x4 trans;
			vehicle->vehicle->getWheelInfo(i).m_worldTransform.getOpenGLMatrix(*trans.v);
			trans.Transpose();

			float3 scale = trans.GetScale();
			w_trs->SetGlobal(trans);
			w_trs->SetScale(scale);
		}
	}
	*/
	//Updating turn animation

	//Player 1 animation
	if (p1_animation != nullptr)
	{
		UpdateP1Animation();
	}

	//Player 2 animation
	if (p2_animation != nullptr)
	{
		UpdateP2Animation();
	}
	
}

void ComponentCar::RenderWithoutCar()
{
	//RENDERING CHASIS

	Cube_P chasis;
	chasis.size = chasis_size;
	chasis.transform = game_object->transform->GetGlobalMatrix().Transposed();
	float3 pos, scal;
	float3x3 rot;
	chasis.transform.Decompose(pos, rot, scal);
	float3 realOffset = rot * chasis_offset;
	chasis.transform = chasis.transform.Transposed() * chasis.transform.Translate(chasis_offset);
	chasis.transform.Transpose();
	chasis.Render();

	//RENDERING WHEELS

	Cylinder_P wheel;
	float3 wheelOffset;
	int _x, _z;
	for (int i = 0; i < 4; i++)
	{
		wheel.radius = wheel_radius;
		wheel.height = wheel_width;

		wheel.transform = game_object->transform->GetGlobalMatrix().Transposed();
		if (i == 0) { _x = 1; _z = 1; }
		else if (i == 1) { _x = -1; _z = -1; }
		else if (i == 2) { _x = -1; _z = 1; }
		else { _x = 1; _z = -1; }

		wheelOffset = chasis_offset;
		wheelOffset += float3((-chasis_size.x / 2.0f + 0.1f * wheel_width) * _x, connection_height - chasis_size.y / 2.0f, (-chasis_size.z / 2.0f + wheel_radius) * _z);

		realOffset = rot * wheelOffset;
		wheel.transform = wheel.transform.Transposed() * wheel.transform.Translate(wheelOffset);
		wheel.transform.Transpose();

		wheel.transform.Translate(realOffset);

		wheel.Render();
	}
}

void ComponentCar::Save(Data& file) const
{
	Data data;
	data.AppendInt("type", type);
	data.AppendUInt("UUID", uuid);
	data.AppendBool("active", active);

	//Common on both cars
	//Game loop settings
	data.AppendFloat("lose_height", lose_height);

	//Chassis settings
	data.AppendFloat3("chasis_size", chasis_size.ptr());
	data.AppendFloat3("chasis_offset", chasis_offset.ptr());

	//Turn over
	data.AppendFloat("turn_over_reset_time", turn_over_reset_time);

	//Max turn change
	data.AppendBool("limit_to_a_turn_max", limit_to_a_turn_max);
	data.AppendBool("accelerated_change", accelerated_change);

	data.AppendInt("current_max_turn_change_mode", current_max_turn_change_mode);

	//Controls settings , Unique for each--------------

	//Wood -------------------------------
	//Acceleration
	data.AppendFloat("wood_acceleration", wood.accel_force);
	data.AppendFloat("wood_max_speed", wood.max_velocity);
	data.AppendFloat("wood_min_speed", wood.min_velocity);
	data.AppendFloat("wood_fake_break", wood.decel_brake);

	//Turn 
	data.AppendFloat("wood_base_turn_max", wood.base_turn_max);
	data.AppendFloat("wood_turn_speed", wood.turn_speed);
	data.AppendFloat("wood_turn_speed_joystick", wood.turn_speed_joystick);

	data.AppendFloat("wood_time_to_idle", wood.time_to_idle);
	data.AppendBool("wood_idle_turn_by_interpolation", wood.idle_turn_by_interpolation);

	//Max turn change
	data.AppendFloat("wood_velocity_to_change", wood.velocity_to_begin_change);
	data.AppendFloat("wood_turn_max_limit", wood.turn_max_limit);

	data.AppendFloat("wood_base_max_turn_change_speed", wood.base_max_turn_change_speed);
	data.AppendFloat("wood_base_max_turn_change_accel", wood.base_max_turn_change_accel);

	

	//Push
	data.AppendFloat("wood_push_force", wood.push_force);
	data.AppendFloat("wood_push_speed_per", wood.push_speed_per);

	//Brake
	data.AppendFloat("wood_brakeForce", wood.brake_force);
	data.AppendFloat("wood_backForce", wood.back_force);
	data.AppendFloat("wood_full_brake_force", wood.full_brake_force);


	//Drift 
	data.AppendFloat("wood_driftRatio", wood.drift_ratio);
	data.AppendFloat("wood_driftMult", wood.drift_mult);
	data.AppendFloat("wood_driftBoost", wood.drift_boost);
	data.AppendFloat("wood_driftMinSpeed", wood.drift_min_speed);
	data.AppendFloat("wood_drift_turn_max", wood.drift_turn_max); 

	//Koji -------------------------------
	//Acceleration
	data.AppendFloat("koji_acceleration", koji.accel_force);
	data.AppendFloat("koji_max_speed", koji.max_velocity);
	data.AppendFloat("koji_min_speed", koji.min_velocity);
	data.AppendFloat("koji_fake_break", koji.decel_brake);

	//Turn 
	data.AppendFloat("koji_base_turn_max", koji.base_turn_max);
	data.AppendFloat("koji_turn_speed", koji.turn_speed);
	data.AppendFloat("koji_turn_speed_joystick", koji.turn_speed_joystick);

	data.AppendFloat("koji_time_to_idle", koji.time_to_idle);
	data.AppendBool("koji_idle_turn_by_interpolation", koji.idle_turn_by_interpolation);

	//Max turn change
	data.AppendFloat("koji_velocity_to_change", koji.velocity_to_begin_change);
	data.AppendFloat("koji_turn_max_limit", koji.turn_max_limit);

	data.AppendFloat("koji_base_max_turn_change_speed", koji.base_max_turn_change_speed);
	data.AppendFloat("koji_base_max_turn_change_accel", koji.base_max_turn_change_accel);



	//Push
	data.AppendFloat("koji_push_force", koji.push_force);
	data.AppendFloat("koji_push_speed_per", koji.push_speed_per);

	//Brake
	data.AppendFloat("koji_brakeForce", koji.brake_force);
	data.AppendFloat("koji_backForce", koji.back_force);
	data.AppendFloat("koji_full_brake_force", koji.full_brake_force);


	//Drift 
	data.AppendFloat("koji_driftRatio", koji.drift_ratio);
	data.AppendFloat("koji_driftMult", koji.drift_mult);
	data.AppendFloat("koji_driftBoost", koji.drift_boost);
	data.AppendFloat("koji_driftMinSpeed", koji.drift_min_speed);
	data.AppendFloat("koji_drift_turn_max", koji.drift_turn_max);

	//Turbos-------
	//Mini turbo
	
		data.AppendFloat("miniturbo_accel_boost", mini_turbo.accel_boost);

		data.AppendFloat("miniturbo_speed_boost", mini_turbo.speed_boost);
		data.AppendFloat("miniturbo_turbo_speed", mini_turbo.turbo_speed);
		data.AppendFloat("miniturbo_deceleration", mini_turbo.deceleration);
		data.AppendFloat("miniturbo_time", mini_turbo.time);

		data.AppendBool("miniturbo_accel_per", mini_turbo.per_ac);
		data.AppendBool("miniturbo_speed_per", mini_turbo.per_sp);
		data.AppendBool("miniturbo_speed_direct", mini_turbo.speed_direct);
		data.AppendBool("miniturbo_speed_decrease", mini_turbo.speed_decrease);

		//Drift turbo 2

		data.AppendFloat("drift_turbo_2_accel_boost", drift_turbo_2.accel_boost);

		data.AppendFloat("drift_turbo_2_speed_boost", drift_turbo_2.speed_boost);
		data.AppendFloat("drift_turbo_2_turbo_speed", drift_turbo_2.turbo_speed);
		data.AppendFloat("drift_turbo_2_deceleration", drift_turbo_2.deceleration);
		data.AppendFloat("drift_turbo_2_time", drift_turbo_2.time);

		data.AppendBool("drift_turbo_2_accel_per", drift_turbo_2.per_ac);
		data.AppendBool("drift_turbo_2_speed_per", drift_turbo_2.per_sp);
		data.AppendBool("drift_turbo_2_speed_direct", drift_turbo_2.speed_direct);
		data.AppendBool("drift_turbo_2_speed_decrease", drift_turbo_2.speed_decrease);

		//Drift turbo 3

		data.AppendFloat("drift_turbo_3_accel_boost", drift_turbo_3.accel_boost);

		data.AppendFloat("drift_turbo_3_speed_boost", drift_turbo_3.speed_boost);
		data.AppendFloat("drift_turbo_3_turbo_speed", drift_turbo_3.turbo_speed);
		data.AppendFloat("drift_turbo_3_deceleration", drift_turbo_3.deceleration);
		data.AppendFloat("drift_turbo_3_time", drift_turbo_3.time);

		data.AppendBool("drift_turbo_3_accel_per", drift_turbo_3.per_ac);
		data.AppendBool("drift_turbo_3_speed_per", drift_turbo_3.per_sp);
		data.AppendBool("drift_turbo_3_speed_direct", drift_turbo_3.speed_direct);
		data.AppendBool("drift_turbo_3_speed_decrease", drift_turbo_3.speed_decrease);

		//TurboPad
		data.AppendFloat("turbo_pad_accel_boost", turbo_pad.accel_boost);
						  
		data.AppendFloat("turbo_pad_speed_boost", turbo_pad.speed_boost);
		data.AppendFloat("turbo_pad_turbo_speed", turbo_pad.turbo_speed);
		data.AppendFloat("turbo_pad_deceleration", turbo_pad.deceleration);
		data.AppendFloat("turbo_pad_time", turbo_pad.time);

		data.AppendBool("turbo_pad_accel_per", turbo_pad.per_ac);
		data.AppendBool("turbo_pad_speed_per", turbo_pad.per_sp);
		data.AppendBool("turbo_pad_speed_direct", turbo_pad.speed_direct);
		data.AppendBool("turbo_pad_speed_decrease", turbo_pad.speed_decrease);

		//Rocket turbo 

		data.AppendFloat("rocket_turbo_accel_boost", rocket_turbo.accel_boost);

		data.AppendFloat("rocket_turbo_speed_boost", rocket_turbo.speed_boost);
		data.AppendFloat("rocket_turbo_turbo_speed", rocket_turbo.turbo_speed);
		data.AppendFloat("rocket_turbo_deceleration", rocket_turbo.deceleration);
		data.AppendFloat("rocket_turbo_time", rocket_turbo.time);

		data.AppendBool("rocket_turbo_accel_per", rocket_turbo.per_ac);
		data.AppendBool("rocket_turbo_speed_per", rocket_turbo.per_sp);
		data.AppendBool("rocket_turbo_speed_direct", rocket_turbo.speed_direct);
		data.AppendBool("rocket_turbo_speed_decrease", rocket_turbo.speed_decrease);
	


	//data.AppendFloat("kick_cooldown", kickCooldown);
	//--------------------------------------------------
	//Wheel settings
	data.AppendFloat("connection_height", connection_height);
	data.AppendFloat("wheel_radius", wheel_radius);
	data.AppendFloat("wheel_width", wheel_width);

	// Saving UUID's GameObjects linked as wheels on Component Car
	if (wheels_go[0]) data.AppendUInt("Wheel Front Left", wheels_go[0]->GetUUID());
	if (wheels_go[1]) data.AppendUInt("Wheel Front Right", wheels_go[1]->GetUUID());
	if (wheels_go[2]) data.AppendUInt("Wheel Back Left", wheels_go[2]->GetUUID());
	if (wheels_go[3]) data.AppendUInt("Wheel Back Right", wheels_go[3]->GetUUID());	

	//Car physics settings
	data.AppendFloat("mass", car->mass);
	data.AppendFloat("suspensionStiffness", car->suspensionStiffness);
	data.AppendFloat("suspensionCompression", car->suspensionCompression);
	data.AppendFloat("suspensionDamping", car->suspensionDamping);
	data.AppendFloat("suspensionRestLength", suspensionRestLength);
	data.AppendFloat("maxSuspensionTravelCm", car->maxSuspensionTravelCm);
	data.AppendFloat("frictionSlip", car->frictionSlip);
	data.AppendFloat("maxSuspensionForce", car->maxSuspensionForce);


	//Hitodamas
	data.AppendInt("max_hitodamas", max_hitodamas);
	data.AppendInt("bonus_hitodamas", bonus_hitodamas);

	file.AppendArrayValue(data);
}

void ComponentCar::Load(Data& conf)
{
	uuid = conf.GetUInt("UUID");
	active = conf.GetBool("active");

	//Game loop settings
	lose_height = conf.GetFloat("lose_height");

	//Chassis settings
	chasis_size = conf.GetFloat3("chasis_size");
	chasis_offset = conf.GetFloat3("chasis_offset");

	//Turn change over time
	limit_to_a_turn_max = conf.GetBool("limit_to_a_turn_max");
	accelerated_change = conf.GetBool("accelerated_change");

	current_max_turn_change_mode = MAX_TURN_CHANGE_MODE(conf.GetInt("current_max_turn_change_mode"));

	//Gameplay settings-----------------
	//Turn over
	turn_over_reset_time = conf.GetFloat("turn_over_reset_time");
	if(turn_over_reset_time < 0.2f)
	{
		turn_over_reset_time = 4.0f;
	}

	//Wood car-------------------------------------------
	//Acceleration
	wood.accel_force = conf.GetFloat("wood_acceleration"); 
	wood.max_velocity = conf.GetFloat("wood_max_speed");
	wood.min_velocity = conf.GetFloat("wood_min_speed");
	wood.decel_brake = conf.GetFloat("wood_fake_break");


	//Turn 
	wood.base_turn_max = conf.GetFloat("wood_base_turn_max");
	wood.turn_speed = conf.GetFloat("wood_turn_speed");
	wood.turn_speed_joystick = conf.GetFloat("wood_turn_speed_joystick");

	wood.time_to_idle = conf.GetFloat("wood_time_to_idle");
	wood.idle_turn_by_interpolation = conf.GetBool("wood_idle_turn_by_interpolation");

	//Max turn change
	wood.velocity_to_begin_change = conf.GetFloat("wood_velocity_to_change");
	wood.turn_max_limit = conf.GetFloat("wood_turn_max_limit");

	wood.base_max_turn_change_speed = conf.GetFloat("wood_base_max_turn_change_speed");
	wood.base_max_turn_change_accel = conf.GetFloat("wood_base_max_turn_change_accel");
	

	//Push
	wood.push_force = conf.GetFloat("wood_push_force");
	wood.push_speed_per = conf.GetFloat("wood_push_speed_per");

	//Brake
	wood.brake_force = conf.GetFloat("wood_brakeForce");
	wood.back_force = conf.GetFloat("wood_backForce");
	wood.full_brake_force = conf.GetFloat("wood_full_brake_force");

	//Drifting settings
	wood.drift_ratio = conf.GetFloat("wood_driftRatio");
	wood.drift_mult = conf.GetFloat("wood_driftMult");
	wood.drift_boost = conf.GetFloat("wood_driftBoost");
	wood.drift_min_speed = conf.GetFloat("wood_driftMinSpeed");
	wood.drift_turn_max = conf.GetFloat("wood_drift_turn_max");
	//-----------------------------------------


	//Koji car-------------------------------------------
	//Acceleration
	koji.accel_force = conf.GetFloat("koji_acceleration");
	koji.max_velocity = conf.GetFloat("koji_max_speed");
	koji.min_velocity = conf.GetFloat("koji_min_speed");
	koji.decel_brake = conf.GetFloat("koji_fake_break");


	//Turn 
	koji.base_turn_max = conf.GetFloat("koji_base_turn_max");
	koji.turn_speed = conf.GetFloat("koji_turn_speed");
	koji.turn_speed_joystick = conf.GetFloat("koji_turn_speed_joystick");

	koji.time_to_idle = conf.GetFloat("koji_time_to_idle");
	koji.idle_turn_by_interpolation = conf.GetBool("koji_idle_turn_by_interpolation");

	//Max turn change
	koji.velocity_to_begin_change = conf.GetFloat("koji_velocity_to_change");
	koji.turn_max_limit = conf.GetFloat("koji_turn_max_limit");

	koji.base_max_turn_change_speed = conf.GetFloat("koji_base_max_turn_change_speed");
	koji.base_max_turn_change_accel = conf.GetFloat("koji_base_max_turn_change_accel");


	//Push
	koji.push_force = conf.GetFloat("koji_push_force");
	koji.push_speed_per = conf.GetFloat("koji_push_speed_per");

	//Brake
	koji.brake_force = conf.GetFloat("koji_brakeForce");
	koji.back_force = conf.GetFloat("koji_backForce");
	wood.full_brake_force = conf.GetFloat("koji_full_brake_force");

	//Drifting settings
	koji.drift_ratio = conf.GetFloat("koji_driftRatio");
	koji.drift_mult = conf.GetFloat("koji_driftMult");
	koji.drift_boost = conf.GetFloat("koji_driftBoost");
	koji.drift_min_speed = conf.GetFloat("koji_driftMinSpeed");
	koji.drift_turn_max = conf.GetFloat("koji_drift_turn_max");
	//-----------------------------------------

	//Turbo
	//Mini turbo
	mini_turbo.accel_boost = conf.GetFloat("miniturbo_accel_boost");
	mini_turbo.speed_boost = conf.GetFloat("miniturbo_speed_boost");
	mini_turbo.turbo_speed = conf.GetFloat("miniturbo_turbo_speed");
	mini_turbo.deceleration = conf.GetFloat("miniturbo_deceleration");
	mini_turbo.time = conf.GetFloat("miniturbo_time");
	
	mini_turbo.per_ac = conf.GetBool("miniturbo_accel_per");
	mini_turbo.per_sp = conf.GetBool("miniturbo_speed_per");
	mini_turbo.speed_direct = conf.GetBool("miniturbo_speed_direct");
	mini_turbo.speed_decrease = conf.GetBool("miniturbo_speed_decrease");

	//Drift turbo 2
	drift_turbo_2.accel_boost = conf.GetFloat("drift_turbo_2_accel_boost");
	drift_turbo_2.speed_boost = conf.GetFloat("drift_turbo_2_speed_boost");
	drift_turbo_2.turbo_speed = conf.GetFloat("drift_turbo_2_turbo_speed");
	drift_turbo_2.deceleration = conf.GetFloat("drift_turbo_2_deceleration");
	drift_turbo_2.time = conf.GetFloat("drift_turbo_2_time");

	drift_turbo_2.per_ac = conf.GetBool("drift_turbo_2_accel_per");
	drift_turbo_2.per_sp = conf.GetBool("drift_turbo_2_speed_per");
	drift_turbo_2.speed_direct = conf.GetBool("drift_turbo_2_speed_direct");
	drift_turbo_2.speed_decrease = conf.GetBool("drift_turbo_2_speed_decrease");

	//Drift turbo 3
	drift_turbo_3.accel_boost = conf.GetFloat("drift_turbo_3_accel_boost");
	drift_turbo_3.speed_boost = conf.GetFloat("drift_turbo_3_speed_boost");
	drift_turbo_3.turbo_speed = conf.GetFloat("drift_turbo_3_turbo_speed");
	drift_turbo_3.deceleration = conf.GetFloat("drift_turbo_3_deceleration");
	drift_turbo_3.time = conf.GetFloat("drift_turbo_3_time");

	drift_turbo_3.per_ac = conf.GetBool("drift_turbo_3_accel_per");
	drift_turbo_3.per_sp = conf.GetBool("drift_turbo_3_speed_per");
	drift_turbo_3.speed_direct = conf.GetBool("drift_turbo_3_speed_direct");
	drift_turbo_3.speed_decrease = conf.GetBool("drift_turbo_3_speed_decrease");

	//Rocket
	rocket_turbo.accel_boost = conf.GetFloat("rocket_turbo_accel_boost");
	rocket_turbo.speed_boost = conf.GetFloat("rocket_turbo_speed_boost");
	rocket_turbo.turbo_speed = conf.GetFloat("rocket_turbo_turbo_speed");
	rocket_turbo.deceleration = conf.GetFloat("rocket_turbo_deceleration");
	rocket_turbo.time = conf.GetFloat("rocket_turbo_time");

	rocket_turbo.per_ac = conf.GetBool("rocket_turbo_accel_per");
	rocket_turbo.per_sp = conf.GetBool("rocket_turbo_speed_per");
	rocket_turbo.speed_direct = conf.GetBool("rocket_turbo_speed_direct");
	rocket_turbo.speed_decrease = conf.GetBool("rocket_turbo_speed_decrease");
	

	//kickCooldown = conf.GetFloat("kick_cooldown");
	//Wheel settings
	connection_height = conf.GetFloat("connection_height");
	wheel_radius = conf.GetFloat("wheel_radius");
	wheel_width = conf.GetFloat("wheel_width");

	// Posting events to further loading of GameObject wheels when all have been loaded)
	if (conf.GetUInt("Wheel Front Left") != 0)
	{
		EventLinkGos *ev = new EventLinkGos((GameObject**)&wheels_go[0], conf.GetUInt("Wheel Front Left"));
		App->event_queue->PostEvent(ev);
	}

	if (conf.GetUInt("Wheel Front Right") != 0)
	{
		EventLinkGos *ev = new EventLinkGos((GameObject**)&wheels_go[1], conf.GetUInt("Wheel Front Right"));
		App->event_queue->PostEvent(ev);
	}

	if (conf.GetUInt("Wheel Back Left") != 0)
	{
		EventLinkGos *ev = new EventLinkGos((GameObject**)&wheels_go[2], conf.GetUInt("Wheel Back Left"));
		App->event_queue->PostEvent(ev);
	}

	if (conf.GetUInt("Wheel Back Right") != 0)
	{
		EventLinkGos *ev = new EventLinkGos((GameObject**)&wheels_go[3], conf.GetUInt("Wheel Back Right"));
		App->event_queue->PostEvent(ev);
	}

	//Car settings
	car->mass = conf.GetFloat("mass");
	car->suspensionStiffness = conf.GetFloat("suspensionStiffness");
	car->suspensionCompression = conf.GetFloat("suspensionCompression");
	car->suspensionDamping = conf.GetFloat("suspensionDamping");
	suspensionRestLength = conf.GetFloat("suspensionRestLength");
	car->maxSuspensionTravelCm = conf.GetFloat("maxSuspensionTravelCm");
	car->frictionSlip = conf.GetFloat("frictionSlip");
	car->maxSuspensionForce = conf.GetFloat("maxSuspensionForce");

	//Hitodamas
	max_hitodamas = conf.GetInt("max_hitodamas");
	bonus_hitodamas = conf.GetInt("bonus_hitodamas");

}

void ComponentCar::OnInspector(bool debug)
{
	string str = (string("Car") + string("##") + std::to_string(uuid));
	if (ImGui::CollapsingHeader(str.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::IsItemClicked(1))
		{
			ImGui::OpenPopup("delete##car");
		}

		if (ImGui::BeginPopup("delete##car"))
		{
			if (ImGui::MenuItem("Delete##car"))
			{
				Remove();
			}
			ImGui::EndPopup();
		}
		

		//Choos car type popup
		if (ImGui::Button("Kart Type :"))
			ImGui::OpenPopup("Kart Type");
		ImGui::SameLine();
		switch(kart->type)
		{
			case T_WOOD:
				ImGui::Text(" Wood ");
				break;
			case T_KOJI:
				ImGui::Text(" Koji Lion ");
				break;
		}
		if (ImGui::BeginPopup("Kart Type"))
		{
			if(ImGui::Selectable("Wood"))
			{
				SetCarType(T_WOOD);
			}
			if(ImGui::Selectable("Koji Lion"))
			{
				SetCarType(T_KOJI);
			}

			ImGui::EndPopup();
		}


		int player_f = (int)front_player;
		if (ImGui::InputInt("Front player joystick", &player_f, 1))
		{
			math::Clamp(player_f, 0, 3);
			front_player = (PLAYER)player_f;
		}
		int player_b = (int)back_player;
		if (ImGui::InputInt("Back player joystick", &player_b, 1))
		{
			math::Clamp(player_b, 0, 3);
			back_player = (PLAYER)player_b;
		}

		ImGui::Text("Bool pushing: %i", (int)pushing);
		int lap = this->lap;
		if (ImGui::InputInt("Current lap", &lap))
		{
			if (lap < 0) lap = 0;
			this->lap = lap;
		}

		ImGui::Text("Last checkpoint: %u", checkpoints);
		if (vehicle)
		{
			if (ImGui::TreeNode("Read Stats"))
			{
				ImGui::Text("");

				ImGui::Text("Top velocity (Km/h) : %f", top_velocity);
				ImGui::Text("Current velocity (Km/h): %f", vehicle->GetKmh());
				ImGui::Text("Velocity boost (%): %f", speed_boost);
				ImGui::Text("");

				ImGui::Text("Current engine force : %f", accel);
				ImGui::Text("Engine force boost (%): %f", accel_boost);
				ImGui::Text("");

				ImGui::Text("Current turn: %f", turn_current);
				ImGui::Text("Current turn max: %f", turn_max);
				ImGui::Text("Turn boost (%): %f", turn_boost);
				ImGui::Text("");

		
				ImGui::Checkbox("On ground", &on_ground);

				bool on_t = current_turbo != T_IDLE;
				ImGui::Checkbox("On turbo", &on_t);

				if (on_t)
				{
					ImGui::Text("Time left: %f", (applied_turbo->time - applied_turbo->timer));
				}
				bool hasItem = has_item;
				if (ImGui::Checkbox("Has item", &hasItem))
				{
					if (hasItem == true)
					{
						PickItem();
					}
				}
				if (turned)
				{
					ImGui::Text("Time to reset: %f", (turn_over_reset_time - timer_start_turned));
				}

				ImGui::Text("");
				ImGui::TreePop();
			}
		}
		if (ImGui::TreeNode("Car settings"))
		{
			if (ImGui::TreeNode("Game loop settings"))
			{
				ImGui::Text("");

				ImGui::Text("Lose height");
				ImGui::SameLine();
				ImGui::DragFloat("##Lheight", &lose_height, 0.1f, 0.0f, 2.0f);

				ImGui::Text("");
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Control settings"))
			{
				if (ImGui::TreeNode("Turn over settings"))
				{
					ImGui::Text("Time to reset");
					ImGui::SameLine();
					if (ImGui::DragFloat("##rt_time", &turn_over_reset_time, 0.1f, 0.5f, 10.0f)) {}

					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Acceleration settings"))
				{
					ImGui::Text("");
					ImGui::Text("Max speed");
					ImGui::SameLine();
					if (ImGui::DragFloat("##MxSpeed", &kart->max_velocity, 1.0f, 0.0f, 1000.0f)) {}

					ImGui::Text("Min speed");
					ImGui::SameLine();
					if (ImGui::DragFloat("##MnSpeed", &kart->min_velocity, 1.0f, -100.0f, 0.0f)) {}

					ImGui::Text("Accel");
					ImGui::SameLine();
					if (ImGui::DragFloat("##AccForce", &kart->accel_force, 1.0f, 0.0f)) {}

					ImGui::Text("Deceleration");
					ImGui::SameLine();
					if (ImGui::DragFloat("##DecelForce", &kart->decel_brake, 1.0f, 0.0f)) {}

					ImGui::Text("");
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Handling settings"))
				{
					ImGui::Text("");

					ImGui::Text("Base turn max");
					ImGui::SameLine();
					if (ImGui::DragFloat("##Turnmax", &kart->base_turn_max, 0.1f, 0.0f, 2.0f)) {}


					ImGui::Text("Turn speed");
					ImGui::SameLine();
					if (ImGui::DragFloat("##Wheel_turn_speed", &kart->turn_speed, 0.01f, 0.0f, 2.0f)) {}

					ImGui::Text("Joystick turn speed");
					if (ImGui::DragFloat("##joystick_turn_speed", &kart->turn_speed_joystick, 0.01f, 0.0f, 2.0f)) {}

					ImGui::Checkbox("Idle turn by interpolation", &kart->idle_turn_by_interpolation);
					if (kart->idle_turn_by_interpolation)
					{
						ImGui::Text("Time to idle turn");
						ImGui::DragFloat("##id_turn_time", &kart->time_to_idle, 0.01f, 0.0f);
					}

					ImGui::Text("");
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Max turn change settings"))
				{
					ImGui::Text("Velocity to begin change");
					ImGui::DragFloat("##v_to_change", &kart->velocity_to_begin_change, 0.1f, 0.0f);

					ImGui::Text("Limit max turn");
					ImGui::DragFloat("##l_max_turn", &kart->turn_max_limit, 1.0f, 0.0f);

					bool by_interpolation = (current_max_turn_change_mode == M_INTERPOLATION);
					bool by_speed = (current_max_turn_change_mode == M_SPEED);
					if (ImGui::Checkbox("By interpolation", &by_interpolation))
						current_max_turn_change_mode = M_INTERPOLATION;
					ImGui::SameLine();
					if (ImGui::Checkbox("By speed", &by_speed))
						current_max_turn_change_mode = M_SPEED;

					if (by_speed)
					{
						ImGui::Text("Base speed of max turn change");
						ImGui::DragFloat("##s_mx_tn_change", &kart->base_max_turn_change_speed, 0.1f);

						ImGui::Checkbox("Limit to a certain turn max", &limit_to_a_turn_max);

						ImGui::Checkbox("Accelerate the change", &accelerated_change);
						if (accelerated_change)
						{
							ImGui::Text("Base accel of max turn change speed");
							ImGui::DragFloat("##a_mx_tn_change", &kart->base_max_turn_change_accel, 0.01f);
						}
					}


					ImGui::Checkbox("Show max turn/ velocity graph", &show_graph);

					if (show_graph)
					{
						float values[14];

						for (int i = 0; i < 14; i++)
						{
							values[i] = GetMaxTurnByCurrentVelocity(float(i)* 10.0f);
						}

						ImGui::PlotLines("Max turn / Velocity", values, 14);
					}

					//NOTE: put a graph so the designers know how it  will affect turn max change over time
					ImGui::TreePop();


				}

				if (ImGui::TreeNode("Brake settings"))
				{
					ImGui::Text("");

					ImGui::Text("Brake force");
					ImGui::SameLine();
					if (ImGui::DragFloat("##Brake_force", &kart->brake_force, 1.0f, 0.0f, 1000.0f)) {}

					ImGui::Text("Back force");
					ImGui::SameLine();
					if (ImGui::DragFloat("##Back_force", &kart->back_force, 1.0f, 0.0f)) {}

					ImGui::Text("Full brake force");
					ImGui::SameLine();
					if (ImGui::DragFloat("##full_br_force", &kart->full_brake_force, 1.0f, 0.0f)) {}

					ImGui::Text("");
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Push settings"))
				{
					ImGui::Text("");

					ImGui::Text("Push force");
					ImGui::SameLine();
					if (ImGui::DragFloat("##push_force", &kart->push_force, 10.0f, 0.0f)) {}

					ImGui::Text("Push speed limit");
					ImGui::SameLine();
					if (ImGui::DragFloat("##push_sp", &kart->push_speed_per, 1.0f, 0.0f, 100.0f)) {}

					ImGui::Text("");
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Turbos"))
				{
					/*for (int i = 0; i < turbos.size(); i++)
					{
					Turbo* tmp = &turbos[i];

					if (ImGui::TreeNode(tmp->name.c_str()))
					{
					ImGui::Checkbox("Accel %", &(tmp->per_ac));
					ImGui::SameLine();
					ImGui::Checkbox("Speed %", &(tmp->per_sp));

					ImGui::DragFloat("Accel boost", &(tmp->accel_boost), 1.0f, 0.0f);
					ImGui::DragFloat("Speed boost", &(tmp->speed_boost), 1.0f, 0.0f);
					ImGui::DragFloat("Duration", &(tmp->time));

					ImGui::Checkbox("Speed decrease", &(tmp->speed_decrease));
					if (tmp->speed_decrease == true)
					{
					ImGui::DragFloat("Deceleration", &(tmp->deceleration), 1.0f, 0.0f);
					}
					ImGui::Checkbox("Direct speed", &(tmp->speed_direct));


					ImGui::TreePop();
					}
					}*/

					if (ImGui::TreeNode("mini turbo"))
					{
						ImGui::Checkbox("Accel %", &mini_turbo.per_ac);
						ImGui::SameLine();
						ImGui::Checkbox("Speed %", &mini_turbo.per_sp);

						ImGui::DragFloat("Accel boost", &mini_turbo.accel_boost, 1.0f, 0.0f);
						ImGui::DragFloat("Speed boost", &mini_turbo.speed_boost, 1.0f, 0.0f);
						ImGui::DragFloat("Duration", &mini_turbo.time);

						ImGui::Checkbox("Speed decrease", &mini_turbo.speed_decrease);
						if (mini_turbo.speed_decrease == true)
						{
							ImGui::DragFloat("Deceleration", &mini_turbo.deceleration, 1.0f, 0.0f);
						}

						ImGui::Checkbox("Direct speed", &mini_turbo.speed_direct);
						/*if (mini_turbo.speed_direct == true)
						{
						ImGui::Checkbox("Speed increase", &mini_turbo.speed_increase);

						if (mini_turbo.speed_increase)
						{
						ImGui::DragFloat("Fake acceleration", &mini_turbo.fake_accel, 1.0f, 0.0f);
						}
						}*/


						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Drift turbo 2"))
					{
						ImGui::Checkbox("Accel %", &drift_turbo_2.per_ac);
						ImGui::SameLine();
						ImGui::Checkbox("Speed %", &drift_turbo_2.per_sp);

						ImGui::DragFloat("Accel boost", &drift_turbo_2.accel_boost, 1.0f, 0.0f);
						ImGui::DragFloat("Speed boost", &drift_turbo_2.speed_boost, 1.0f, 0.0f);
						ImGui::DragFloat("Duration", &drift_turbo_2.time);

						ImGui::Checkbox("Speed decrease", &drift_turbo_2.speed_decrease);
						if (drift_turbo_2.speed_decrease == true)
						{
							ImGui::DragFloat("Deceleration", &drift_turbo_2.deceleration, 1.0f, 0.0f);
						}

						ImGui::Checkbox("Direct speed", &drift_turbo_2.speed_direct);
						/*if (drift_turbo_2.speed_direct == true)
						{
						ImGui::Checkbox("Speed increase", &drift_turbo_2.speed_increase);

						if (drift_turbo_2.speed_increase)
						{
						ImGui::DragFloat("Fake acceleration", &drift_turbo_2.fake_accel, 1.0f, 0.0f);
						}
						}*/

						ImGui::TreePop();
					}


					if (ImGui::TreeNode("Drift turbo 3"))
					{
						ImGui::Checkbox("Accel %", &drift_turbo_3.per_ac);
						ImGui::SameLine();
						ImGui::Checkbox("Speed %", &drift_turbo_3.per_sp);

						ImGui::DragFloat("Accel boost", &drift_turbo_3.accel_boost, 1.0f, 0.0f);
						ImGui::DragFloat("Speed boost", &drift_turbo_3.speed_boost, 1.0f, 0.0f);
						ImGui::DragFloat("Duration", &drift_turbo_3.time);

						ImGui::Checkbox("Speed decrease", &drift_turbo_3.speed_decrease);
						if (drift_turbo_3.speed_decrease == true)
						{
							ImGui::DragFloat("Deceleration", &drift_turbo_3.deceleration, 1.0f, 0.0f);
						}
						ImGui::Checkbox("Direct speed", &drift_turbo_3.speed_direct);

						/*if (drift_turbo_3.speed_direct == true)
						{
						ImGui::Checkbox("Speed increase", &drift_turbo_3.speed_increase);

						if (drift_turbo_3.speed_increase)
						{
						ImGui::DragFloat("Fake acceleration", &drift_turbo_3.fake_accel, 1.0f, 0.0f);
						}
						}*/

						ImGui::TreePop();
					}

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Hitodamas"))
				{
					ImGui::DragInt("Max hitodamas", &max_hitodamas, 1.0f, 0, 100);
					ImGui::DragInt("Bonus hitodamas", &bonus_hitodamas, 1.0f, 0, 100);
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Items"))
				{
					ImGui::DragInt("Max hitodamas", &max_hitodamas, 1.0f, 0, 100);
					ImGui::DragInt("Bonus hitodamas", &bonus_hitodamas, 1.0f, 0, 100);
					if (ImGui::TreeNode("Rocket"))
					{

						if (ImGui::TreeNode("Turbo config"))
						{
							ImGui::Checkbox("Accel %", &rocket_turbo.per_ac);
							ImGui::SameLine();
							ImGui::Checkbox("Speed %", &rocket_turbo.per_sp);

							ImGui::DragFloat("Accel boost", &rocket_turbo.accel_boost, 1.0f, 0.0f);
							ImGui::DragFloat("Speed boost", &rocket_turbo.speed_boost, 1.0f, 0.0f);
							ImGui::DragFloat("Duration", &rocket_turbo.time);

							ImGui::Checkbox("Speed decrease", &rocket_turbo.speed_decrease);
							if (mini_turbo.speed_decrease == true)
							{
								ImGui::DragFloat("Deceleration", &rocket_turbo.deceleration, 1.0f, 0.0f);
							}
							ImGui::Checkbox("Direct speed", &rocket_turbo.speed_direct);

							/*if (rocket_turbo.speed_direct == true)
							{
							ImGui::Checkbox("Speed increase", &rocket_turbo.speed_increase);

							if (rocket_turbo.speed_increase)
							{
							ImGui::DragFloat("Fake acceleration", &rocket_turbo.fake_accel, 1.0f, 0.0f);
							}
							}*/

							ImGui::TreePop();
						}
						ImGui::TreePop();
					}
					ImGui::TreePop();
				}

				ImGui::TreePop();
			}

			if (App->IsGameRunning() == false)
			{
				if (ImGui::TreeNode("Chasis settings"))
				{
					ImGui::Text("Size");
					ImGui::SameLine();
					ImGui::DragFloat3("##Chasis size", chasis_size.ptr(), 0.1f, 0.1f, 5.0f);

					ImGui::Text("Offset");
					ImGui::SameLine();
					ImGui::DragFloat3("##Chasis offset", chasis_offset.ptr(), 0.1f, 0.1f, 5.0f);

					ImGui::Text("Mass");
					ImGui::SameLine();
					ImGui::DragFloat("##Mass", &car->mass, 1.0f, 0.1f, floatMax);

					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Suspension"))
				{
					ImGui::Text("Rest length");
					ImGui::SameLine();
					ImGui::DragFloat("##Suspension rest length", &suspensionRestLength, 0.1f, 0.1f, floatMax);

					ImGui::Text("Max travel (Cm)");
					ImGui::SameLine();
					ImGui::DragFloat("##Max suspension travel Cm", &car->maxSuspensionTravelCm, 1.0f, 0.1f, floatMax);

					ImGui::Text("Stiffness");
					ImGui::SameLine();
					ImGui::DragFloat("##Suspension stiffness", &car->suspensionStiffness, 0.1f, 0.1f, floatMax);

					ImGui::Text("Damping");
					ImGui::SameLine();
					ImGui::DragFloat("##Suspension Damping", &car->suspensionDamping, 1.0f, 0.1f, floatMax);

					ImGui::Text("Max force");
					ImGui::SameLine();
					ImGui::DragFloat("##Max suspension force", &car->maxSuspensionForce, 1.0f, 0.1f, floatMax);

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Wheel settings"))
				{
					ImGui::Text("Connection height");
					ImGui::SameLine();
					ImGui::DragFloat("##Connection height", &connection_height, 0.1f, floatMin, floatMax);

					ImGui::Text("Radius");
					ImGui::SameLine();
					ImGui::DragFloat("##Wheel radius", &wheel_radius, 0.1f, 0.1f, floatMax);

					ImGui::Text("Width");
					ImGui::SameLine();
					ImGui::DragFloat("##Wheel width", &wheel_width, 0.1f, 0.1f, floatMax);
					ImGui::TreePop();
				}

				ImGui::Text("Friction Slip");
				ImGui::SameLine();
				ImGui::DragFloat("##Friction Slip", &car->frictionSlip, 1.0f, 0.1f, floatMax);
			}//Endof IsGameRunning() == false

			ImGui::Separator();
			ImGui::Text("Drifting settings");
			ImGui::NewLine();

			ImGui::Text("Drift exit boost");
			ImGui::InputFloat("Drift exit boost", &kart->drift_boost);

			ImGui::Text("Drift turn max");
			ImGui::InputFloat("Drift turn max", &kart->drift_turn_max);

			ImGui::Text("Drift min speed");
			ImGui::InputFloat("Drift min speed", &kart->drift_min_speed);

			ImGui::Text("Drift multiplier");
			ImGui::InputFloat("Drift mult", &kart->drift_mult);

			ImGui::Text("Drift angle ratio");
			ImGui::DragFloat("##Dr_angle_ratio", &kart->drift_ratio, 0.001, 0.0f, 1.0f);
			
			ImGui::TreePop();

		} //Endof Car settings

		if (ImGui::TreeNode("Wheels"))
		{
			if (App->editor->assign_wheel != -1 && App->editor->wheel_assign != nullptr)
			{
				wheels_go[App->editor->assign_wheel] = App->editor->wheel_assign;
				App->editor->assign_wheel = -1;
				App->editor->wheel_assign = nullptr;
			}

			ImGui::Text("Front Left");
			if (wheels_go[0] != nullptr)
			{
				ImGui::Text(wheels_go[0]->name.c_str());
				ImGui::SameLine();
			}
			if (ImGui::Button("Assign Wheel##1"))
			{
				App->editor->assign_wheel = 0;
				App->editor->wheel_assign = nullptr;
			}
			ImGui::Text("Front Right");
			if (wheels_go[1] != nullptr)
			{
				ImGui::Text(wheels_go[1]->name.c_str());
				ImGui::SameLine();
			}
			if (ImGui::Button("Assign Wheel##2"))
			{
				App->editor->assign_wheel = 1;
				App->editor->wheel_assign = nullptr;

			}
			ImGui::Text("Back Left");
			if (wheels_go[2] != nullptr)
			{
				ImGui::Text(wheels_go[2]->name.c_str());
				ImGui::SameLine();
			}
			if (ImGui::Button("Assign Wheel##3"))
			{
				App->editor->assign_wheel = 2;
				App->editor->wheel_assign = nullptr;
			}
			ImGui::Text("Back Right");
			if (wheels_go[3] != nullptr)
			{
				ImGui::Text(wheels_go[3]->name.c_str());
				ImGui::SameLine();
			}
			if (ImGui::Button("Assign Wheel##4"))
			{
				App->editor->assign_wheel = 3;
				App->editor->wheel_assign = nullptr;
			}
			ImGui::TreePop();
		}

		if (ImGui::Button("Assign Item"))
		{
			App->editor->assign_wheel = -1;
			App->editor->wheel_assign = nullptr;
			App->editor->assign_item = true;
			App->editor->to_assign_item = this;
		}

	}//Endof Collapsing header
}


