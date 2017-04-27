#include "ComponentParticleSystem.h"

#include "imgui\imgui.h"
#include "Data.h"

#include "Glew\include\glew.h"
#include <gl/GL.h>
#include <gl/GLU.h>

#include "ResourceFileTexture.h"

#include "Application.h"
#include "Assets.h"
#include "GameObject.h"
#include "Time.h"
#include "OpenGLFunc.h"

#include "ModuleEditor.h"
#include "ModuleWindow.h"
#include "ModuleRenderer3D.h"
#include "ModuleCamera3D.h"

#include "ComponentMesh.h"
#include "ComponentTransform.h"
#include "ComponentCamera.h"

#include "Brofiler\include\Brofiler.h"

#include <string>
#include <algorithm>
using namespace std;

ComponentParticleSystem::ComponentParticleSystem(ComponentType type, GameObject* game_object) : Component(type, game_object)
{
	BROFILER_CATEGORY("ComponentParticleSystem::Init", Profiler::Color::Navy);

	particles_container.resize(top_max_particles);
	alive_particles_position.resize(top_max_particles);

	glGenBuffers(1, &position_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	glBufferData(GL_ARRAY_BUFFER, top_max_particles * 3 * sizeof(float), NULL, GL_STREAM_DRAW);
}

ComponentParticleSystem::~ComponentParticleSystem()
{
	
}

void ComponentParticleSystem::OnInspector(bool debug)
{
	if (ImGui::CollapsingHeader("ParticleSystem", ImGuiTreeNodeFlags_DefaultOpen))
	{
		InspectorDelete();

		//Main Options
		ImGui::Text("Lifetime: "); ImGui::SameLine(); ImGui::DragFloat("###ps_lifetime", &life_time, 1.0f, 0.0f, 1000.0f);
		ImGui::Text("Speed: "); ImGui::SameLine(); ImGui::DragFloat("###ps_speed", &speed, 1.0f, 0.0, 1000.0f);
		ImGui::Text("Size: "); ImGui::SameLine(); ImGui::DragFloat("###ps_size", &size, 1.0f, 0.0, 1000.0f);
		ImGui::Text("Max particles: "); ImGui::SameLine(); ImGui::DragInt("###max_particles", &max_particles, 1, 0, 1000);
		ImGui::Text("Play On Awake: "); ImGui::SameLine(); ImGui::Checkbox("###ps_play_awake", &play_on_awake);

		ImGui::Text("Emission rate: "); ImGui::SameLine(); 
		if (ImGui::DragFloat("###ps_emission", &emission_rate, 1.0f, 0.0f, 500.0f))
		{
			spawn_time = 1.0f / emission_rate;
			spawn_timer = 0.0f;
		}

		//Debug
		if (debug)
		{
			//ImGui::Text("Life particles %i", life_particles);
		}

		//Render
		if (ImGui::CollapsingHeader("Render ###ps_render"))
		{
			if (texture)
				ImGui::Image((ImTextureID)texture->GetTexture(), ImVec2(50, 50));

			ImGui::Text("Change sprite: ");
			if (ImGui::BeginMenu("###ps_change_tex"))
			{
				InspectorChangeTexture();
				ImGui::EndMenu();
			}
		}

		InspectorSimulation();
	}
}

void ComponentParticleSystem::Save(Data & file) const
{
	Data data;
	data.AppendInt("type", type);
	data.AppendUInt("UUID", uuid);
	data.AppendBool("active", active);

	data.AppendFloat("life_time", life_time);
	data.AppendInt("max_particles", max_particles);
	data.AppendFloat("emission_rate", emission_rate);
	data.AppendFloat("speed", speed);
	data.AppendFloat("size", size);
	data.AppendBool("play_on_awake", play_on_awake);

	//Render
	if (texture)
		data.AppendString("texture", texture->GetFile());
	else
		data.AppendString("texture", "");

	file.AppendArrayValue(data);
}

void ComponentParticleSystem::Load(Data & conf)
{
	uuid = conf.GetUInt("UUID");
	active = conf.GetBool("active");

	life_time = conf.GetFloat("life_time");
	max_particles = conf.GetInt("max_particles");
	emission_rate = conf.GetFloat("emission_rate");
	spawn_time = 1.0f / emission_rate;
	speed = conf.GetFloat("speed");
	size = conf.GetFloat("size");
	play_on_awake = conf.GetBool("play_on_awake");

	string tex_path = conf.GetString("texture");
	if (tex_path.size() > 0)
	{
		ResourceFileTexture* rc_tmp = (ResourceFileTexture*)App->resource_manager->LoadResource(tex_path, ResourceFileType::RES_TEXTURE);
		if (rc_tmp)
			texture = rc_tmp;
		else
		{
			LOG("[ERROR] Loading failure on particle system %s %s", game_object->name.data(), tex_path.data());
			App->editor->DisplayWarning(WarningType::W_ERROR, "Loading failure on particle system %s %s", game_object->name.data(), tex_path.data());
		}
	}
}

void ComponentParticleSystem::Update()
{
	BROFILER_CATEGORY("ComponentParticleSystem::Update", Profiler::Color::Navy);

	if (playing_editor || is_playing)
	{
		spawn_timer += time->RealDeltaTime();

		if (spawn_timer >= spawn_time)
		{
			int num_particles_to_spawn = spawn_timer / spawn_time;

			for (int i = 0; i < num_particles_to_spawn; i++)
				SpawnParticle();

			spawn_timer -= spawn_time * num_particles_to_spawn;
		}

	}

}

void ComponentParticleSystem::PostUpdate()
{
	BROFILER_CATEGORY("ComponentParticleSystem::UpdatePositions", Profiler::Color::Navy);

	if (playing_editor || is_playing)
	{
		float dt = time->RealDeltaTime();//time->DeltaTime();

		num_alive_particles = 0;

		float3 origin = game_object->GetGlobalMatrix().TranslatePart();
		Quat rotation = game_object->GetGlobalMatrix().RotatePart().ToQuat();

		//Update positions
		for (int i = 0; i < top_max_particles; i++)
		{
			Particle& p = particles_container[i];

			if (p.life > 0.0f)
			{
				p.life -= dt;

				if (p.life > 0.0f)
				{
					p.position = origin + (rotation * p.speed) * (life_time - p.life);
					++num_alive_particles;
				}
			}
			else
				p.cam_distance = -1.0f;

		}

		App->renderer3D->AddToDrawParticle(this);

		if(playing_editor)
			simulation_time += dt;
	}
	
}

void ComponentParticleSystem::OnPlay()
{
	if (play_on_awake)
	{
		is_playing = true;
	}
}

void ComponentParticleSystem::OnPause()
{
	//Do not create new particles and do not update positions
}

void ComponentParticleSystem::OnStop()
{
	if (is_playing)
	{
		is_playing = false;
		StopAll();
	}
}

unsigned int ComponentParticleSystem::GetTextureId() const
{
	return (texture) ? texture->GetTexture() : 0;
}

void ComponentParticleSystem::SortParticles(ComponentCamera * cam)
{
	float3 cam_pos;
	if (cam != App->camera->GetEditorCamera())
		cam_pos = cam->GetGameObject()->GetGlobalMatrix().TranslatePart();
	else
		cam_pos = cam->GetPos();

	for (int i = 0; i < max_particles; i++)
	{
		particles_container[i].cam_distance = cam_pos.DistanceSq(particles_container[i].position);
	}

	std::sort(particles_container.begin(), particles_container.end());

	for (int i = 0; i < num_alive_particles; i++)
	{
		alive_particles_position[i] = particles_container[i].position;
	}

	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	glBufferData(GL_ARRAY_BUFFER, top_max_particles * 3 * sizeof(float), NULL, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, num_alive_particles * sizeof(float) * 3, alive_particles_position.data());
}

void ComponentParticleSystem::StopAll()
{
	for (int i = 0; i < top_max_particles; i++)
		particles_container[i].life = -1.0f;

	last_used_particle = 0;
	num_alive_particles = 0;
}

void ComponentParticleSystem::InspectorDelete()
{
	if (ImGui::IsItemClicked(1))
		ImGui::OpenPopup("delete##ps_delete");
	if (ImGui::BeginPopup("delete##ps_delete"))
	{
		if (ImGui::MenuItem("Delete"))
			Remove();
		ImGui::EndPopup();
	}
}

void ComponentParticleSystem::InspectorChangeTexture()
{
	vector<string> textures;
	App->editor->assets->GetAllFilesByType(FileType::IMAGE, textures);

	for (vector<string>::iterator it = textures.begin(); it != textures.end(); ++it)
	{
		if (ImGui::MenuItem((*it).data()))
		{
			string u_sampler2d = App->resource_manager->FindFile(*it);
			ResourceFileTexture* rc_tmp = (ResourceFileTexture*)App->resource_manager->LoadResource(u_sampler2d, ResourceFileType::RES_TEXTURE);
			if (rc_tmp)
			{
				if (texture)
					texture->Unload();

				texture = rc_tmp;
			}
			else
			{
				LOG("[ERROR] Loading failure on particle system %s %s", game_object->name.data(), (*it).data());
				App->editor->DisplayWarning(WarningType::W_ERROR, "Loading failure on particle system %s %s", game_object->name.data(), (*it).data());
			}
		}
	}
}

void ComponentParticleSystem::InspectorSimulation()
{
	ImGui::SetNextWindowPos(ImVec2(App->window->GetScreenWidth() * 3 / 5, 600));
	bool open = true;
	ImGui::Begin("##ps_simulation", &open, ImVec2(0, 0), 0.6f, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::Button("Play##ps_stop"))
	{
		playing_editor = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop##ps_stop"))
	{
		playing_editor = false;
		simulation_time = 0.0f;
		StopAll();
	}
	ImGui::Text("Playback Time: %.2f", simulation_time);
	ImGui::End();
}

void ComponentParticleSystem::SpawnParticle()
{
	int id = FindUnusedParticle();

	Particle& p = particles_container[id];

	p.life = life_time;
	p.position = math::float3(0.0f);
	p.speed = math::float3(0, speed, 0);
}

int ComponentParticleSystem::FindUnusedParticle()
{
	for (int i = last_used_particle; i < top_max_particles; i++)
	{
		if (particles_container[i].life < 0)
		{
			last_used_particle = i;
			return i;
		}
	}

	for (int i = 0; i < last_used_particle; i++)
	{
		if (particles_container[i].life < 0)
		{
			last_used_particle = i;
			return i;
		}
	}

	return 0;
}

bool Particle::operator<(Particle & b)
{
	if (this->life < 0 && b.life < 0) return this->life > b.life;
	if (this->life > 0 && b.life < 0) return true;
	if (this->life < 0 && b.life > 0) return false;
	return this->cam_distance > b.cam_distance;
}
