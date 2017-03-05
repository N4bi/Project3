#include "ComponentAudio.h"

#include "ModuleAudio.h"
#include "ModuleResourceManager.h"
#include "Application.h"

#include "GameObject.h"

#include "imgui\imgui.h"

ComponentAudio::ComponentAudio(ComponentType type, GameObject* game_object) : Component(type, game_object)
{
	App->audio->RegisterGameObject(game_object->GetUUID());
}

ComponentAudio::~ComponentAudio()
{ 
	App->audio->UnregisterGameObject(game_object->GetUUID());
	if(current_event != nullptr) App->resource_manager->UnloadResource(current_event->parent_soundbank->path);
}

void ComponentAudio::Update()
{ }

void ComponentAudio::OnInspector(bool debug)
{
	std::string str = (std::string("Audio Source") + std::string("##") + std::to_string(uuid));
	if (ImGui::CollapsingHeader(str.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::IsItemClicked(1))
		{
			ImGui::OpenPopup("delete##audio");
		}

		if (ImGui::BeginPopup("delete##audio"))
		{
			if (ImGui::MenuItem("Delete"))
			{
				Remove();
			}
			ImGui::EndPopup();
		}

		//Active
		bool is_active = IsActive();
		if (ImGui::Checkbox("###activeAudio", &is_active))
		{
			SetActive(is_active);
		}

		// Event selection
		ImGui::Text("Event: ");
		ImGui::SameLine();

		// Setting current event, from Selection on Inspector or loading form the value of event_id
		if (current_event == nullptr)
		{
			current_event = App->audio->FindEventById(event_id);
			event_selected = current_event != nullptr ? current_event->name : "";
		}			

		std::vector<AudioEvent*> events;
		App->audio->ObtainEvents(events);
		
		if (ImGui::BeginMenu(event_selected.c_str()))
		{
			for (std::vector<AudioEvent*>::iterator it = events.begin(); it != events.end(); ++it)
			{
				if (ImGui::MenuItem((*it)->name.c_str()))
				{
					// TODO: Maybe, the new event shares the same Soundbank...
					// Unloading unused Soundbank.
					if (current_event != nullptr) App->resource_manager->UnloadResource(current_event->parent_soundbank->path);
					// Loading new bank: first Init bank if it has been not loaded and then, the other one
					if (!App->audio->IsInitSoundbankLoaded())
						if (App->resource_manager->LoadResource(App->audio->GetInitLibrarySoundbankPath(), ResourceFileType::RES_SOUNDBANK) != nullptr)  // Init SB
							App->audio->InitSoundbankLoaded();
					rc_audio = (ResourceFileAudio*)App->resource_manager->LoadResource((*it)->parent_soundbank->path, ResourceFileType::RES_SOUNDBANK);  // Other one SB

					event_selected = (*it)->name; // Name to show on Inspector
					current_event = *it;		  // Variable that handles the new event
				}				
			}
			ImGui::EndMenu();			
		}
		
		if (ImGui::Button("PLAY") && current_event != nullptr)
			App->audio->PostEvent(current_event, game_object->GetUUID());
	}
}

void ComponentAudio::Save(Data & file)const
{
	Data data;
	data.AppendInt("type", type);
	data.AppendUInt("UUID", uuid);
	data.AppendBool("active", active);

	// Saving Init Soundbank information. Must be loaded first when loading new scenes.
	data.AppendString("init_soundbank_lib_path", App->audio->GetInitLibrarySoundbankPath());

	// Current event on this component Audio
	if (current_event)
	{
		data.AppendUInt("event_id", current_event->id);
		data.AppendString("event_name", current_event->name.c_str());
		data.AppendString("soundbank_lib_path", current_event->parent_soundbank->path.c_str());
	}		

	file.AppendArrayValue(data);
}

void ComponentAudio::Load(Data & conf)
{
	uuid = conf.GetUInt("UUID");
	active = conf.GetBool("active");

	// It's mandatory to load Init Soundbank first
	if (!App->audio->IsInitSoundbankLoaded())
	{
		if (App->resource_manager->LoadResource(conf.GetString("init_soundbank_lib_path"), ResourceFileType::RES_SOUNDBANK) != nullptr)
			App->audio->InitSoundbankLoaded();
	}		

	// There are some events and the corresponding Soundbanks to load?
	event_id = conf.GetUInt("event_id");
	if (event_id != 0)
		rc_audio = (ResourceFileAudio*)App->resource_manager->LoadResource(conf.GetString("soundbank_lib_path"), ResourceFileType::RES_SOUNDBANK);	
}

void ComponentAudio::Remove()
{
	game_object->RemoveComponent(this);
}