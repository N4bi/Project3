#include "ModuleRenderer3D.h"

#include "Application.h"
#include "ModuleGOManager.h"
#include "ModuleLighting.h"
#include "ModuleWindow.h"
#include "ModuleCamera3D.h"
#include "ModuleResourceManager.h"
#include "ModuleEditor.h"
#include "ModulePhysics3D.h"
#include "GameObject.h"
#include "ComponentCamera.h"
#include "ComponentMesh.h"
#include "ComponentMaterial.h"
#include "ComponentTransform.h"
#include "ComponentLight.h"

#include "Glew\include\glew.h"
#include <gl/GL.h>
#include <gl/GLU.h>

#include "ModuleWindow.h"
#include "ModuleCamera3D.h"
#include "ModuleResourceManager.h"
#include "ComponentRectTransform.h"
#include "ComponentUiImage.h"
#include "ComponentCanvas.h"
#include "ComponentUiText.h"
#include "ComponentUiButton.h"

#include "SDL\include\SDL_opengl.h"

#include "ResourceFileMaterial.h"
#include "ResourceFileRenderTexture.h"

#include "Octree.h"
#include "Time.h"

#include "SDL/include/SDL_video.h"

#include "Brofiler\include\Brofiler.h"


#pragma comment (lib, "glu32.lib")    /* link OpenGL Utility lib     */
#pragma comment (lib, "opengl32.lib") /* link Microsoft OpenGL lib   */
#pragma comment (lib, "Glew/libx86/glew32.lib") 

#include "Imgui\imgui.h"
#include "Imgui\imgui_impl_sdl_gl3.h"

#include "OpenGLDebug.h"

ModuleRenderer3D::ModuleRenderer3D(const char* name, bool start_enabled) : Module(name, start_enabled)
{ }

// Destructor
ModuleRenderer3D::~ModuleRenderer3D()
{}

// Called before render is available
bool ModuleRenderer3D::Init(Data& config)
{
	LOG("Creating 3D Renderer context");
	bool ret = true;
	
	//Create context
	context = SDL_GL_CreateContext(App->window->window);
	if(context == NULL)
	{
		LOG("OpenGL context could not be created! SDL_Error: %s\n", SDL_GetError());
		ret = false;
	}

	GLenum gl_enum = glewInit();

	if (GLEW_OK != gl_enum)
	{
		LOG("Glew failed");
	}

	if(ret == true)
	{
		// More information on Debugging GPUs
	    // https://learnopengl.com/#!In-Practice/Debugging
		// https://www.khronos.org/opengl/wiki/Debug_Output
		// http://in2gpu.com/2015/05/29/debugging-opengl-part-ii-debug-output/

		// initialize debug output 
		/*glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(OpenGLDebug::OpenGLDebugCallback, nullptr);
		glDebugMessageControl(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, GL_DEBUG_SEVERITY_MEDIUM, 0, nullptr, GL_TRUE);*/
		
		//Use Vsync
		if(VSYNC && SDL_GL_SetSwapInterval(1) < 0)
			LOG("Warning: Unable to set VSync! SDL Error: %s\n", SDL_GetError());

		//Initialize Projection Matrix
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();

		//Check for error
		GLenum error = glGetError();
		if(error != GL_NO_ERROR)
		{
			LOG("Error initializing OpenGL! %s\n", gluErrorString(error));
			ret = false;
		}

		//Initialize Modelview Matrix
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		//Check for error
		error = glGetError();
		if(error != GL_NO_ERROR)
		{
			LOG("Error initializing OpenGL! %s\n", gluErrorString(error));
			ret = false;
		}
		
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		glClearDepth(1.0f);
		
		//Initialize clear color
		glClearColor(0.17f, 0.17f, 0.17f, 1.0f);

		//Check for error
		error = glGetError();
		if(error != GL_NO_ERROR)
		{
			LOG("Error initializing OpenGL! %s\n", gluErrorString(error));
			ret = false;
		}
		
		GLfloat LightModelAmbient[] = {0.0f, 0.0f, 0.0f, 1.0f};
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, LightModelAmbient);
		
		lights[0].ref = GL_LIGHT0;
		lights[0].ambient.Set(0.25f, 0.25f, 0.25f, 1.0f);
		lights[0].diffuse.Set(0.75f, 0.75f, 0.75f, 1.0f);
		lights[0].SetPos(0.0f, 0.0f, 2.5f);
		lights[0].Init();
		
		GLfloat MaterialAmbient[] = {1.0f, 1.0f, 1.0f, 1.0f};
		glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, MaterialAmbient);

		GLfloat MaterialDiffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
		glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialDiffuse);
		
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		lights[0].Active(true);
		glEnable(GL_LIGHTING);
		glEnable(GL_COLOR_MATERIAL);
	}

	LOG("-------------Versions------------------");
	LOG("OpenGL Version: %s",glGetString(GL_VERSION));
	LOG("Glew Version: %s", glewGetString(GLEW_VERSION));

	// Projection matrix for
	OnResize(App->window->GetScreenWidth(), App->window->GetScreenHeight(), 60.0f);

	ImGui_ImplSdlGL3_Init(App->window->window);
	
	return ret;
}

// PreUpdate: clear buffer
update_status ModuleRenderer3D::PreUpdate()
{
	BROFILER_CATEGORY("ModuleRenderer3d::PreUpdate", Profiler::Color::HotPink)

	if (cameras[0]->properties_modified)
	{
		UpdateProjectionMatrix(cameras[0]);
		cameras[0]->properties_modified = false;
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf((float*)cameras[0]->GetViewMatrix().v);

	// light 0 on cam pos
	lights[0].SetPos(App->camera->GetPosition().x, App->camera->GetPosition().y, App->camera->GetPosition().z);

	for(uint i = 0; i < MAX_LIGHTS; ++i)
		lights[i].Render();

	objects_to_draw.clear();

	return UPDATE_CONTINUE;
}

// PostUpdate present buffer to screen
update_status ModuleRenderer3D::PostUpdate()
{
	BROFILER_CATEGORY("ModuleRenderer3d::PostUpdate", Profiler::Color::MediumOrchid)

	glEnable(GL_CLIP_DISTANCE0);
	//RenderTextures
	vector<Component*> scene_cameras;
	App->go_manager->GetAllComponents(scene_cameras, ComponentType::C_CAMERA);

	for (size_t i = 0; i < scene_cameras.size(); ++i)
	{
		ComponentCamera *cam = (ComponentCamera*)scene_cameras[i];
		if (cam->render_texture)
		{
			DrawScene(cam, true);
		}
	}

	glDisable(GL_CLIP_DISTANCE0);

	for (uint i = 0; i < cameras.size(); i++)
	{
		DrawScene(cameras[i]);
	}

	/*
	glViewport(0, App->window->GetScreenHeight()/2, App->window->GetScreenWidth(), App->window->GetScreenHeight()/2);
	DrawScene(camera);

	glViewport(0, 0, App->window->GetScreenWidth(), App->window->GetScreenHeight() / 2);
	DrawScene(camera);
	*/
	glUseProgram(0);

	ImGui::Render();
	SDL_GL_SwapWindow(App->window->window);
	return UPDATE_CONTINUE;
}

// Called before quitting
bool ModuleRenderer3D::CleanUp()
{
	LOG("Destroying 3D Renderer");
	ImGui_ImplSdlGL3_Shutdown();
	SDL_GL_DeleteContext(context);

	return true;
}


void ModuleRenderer3D::OnResize(int width, int height, float fovy)
{
	glViewport(0, 0, width, height);

	//UpdateProjectionMatrix();

	App->window->SetScreenSize(width, height);
	SendEvent(this, Event::WINDOW_RESIZE);
}

void ModuleRenderer3D::UpdateProjectionMatrix(ComponentCamera* camera)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glLoadMatrixf((GLfloat*)camera->GetProjectionMatrix().v);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

const ComponentCamera* ModuleRenderer3D::GetCamera() const
{
	return cameras[0];
}

void ModuleRenderer3D::SetCamera(ComponentCamera* camera)
{
	cameras.clear();
	if (camera != nullptr)
	{
		for (uint i = 0; i < cameras.size(); i++)
		{
			if (cameras[i] == camera)
				return;
		}
		cameras.push_back(camera);
		UpdateProjectionMatrix(cameras.back());
	}
}

void ModuleRenderer3D::AddCamera(ComponentCamera* camera)
{
	if (camera != nullptr)
	{
		for (uint i = 0; i < cameras.size(); i++)
		{
			if (cameras[i] == camera)
				return;
		}
		cameras.push_back(camera);
	}
}

void ModuleRenderer3D::AddToDraw(GameObject* obj)
{
	if (obj)
	{
		if(obj->IsStatic() == false)
			objects_to_draw.push_back(obj);
	}
}

void ModuleRenderer3D::DrawScene(ComponentCamera* cam, bool has_render_tex)
{
	BROFILER_CATEGORY("ModuleRenderer3D::DrawScene", Profiler::Color::NavajoWhite);

	glViewport(cam->viewport_position.x, cam->viewport_position.y, cam->viewport_size.x, cam->viewport_size.y);

	glLoadIdentity();

	//glMatrixMode(GL_MODELVIEW);
	//glLoadMatrixf((float*)cameras[0]->GetViewMatrix().v);

	UpdateProjectionMatrix(cam);

	int layer_mask = cam->GetLayerMask();

	//Draw UI
	if (App->go_manager->current_scene_canvas != nullptr)
	{
		vector<GameObject*> ui_objects = App->go_manager->current_scene_canvas->GetUI();
		for (vector<GameObject*>::const_iterator obj = ui_objects.begin(); obj != ui_objects.end(); ++obj)
		{
			if (layer_mask == (layer_mask | (1 << (*obj)->layer)))
			{
				if ((*obj)->GetComponent(C_UI_IMAGE) || (*obj)->GetComponent(C_UI_BUTTON))
					DrawUIImage(*obj);
				else if ((*obj)->GetComponent(C_UI_TEXT))
					DrawUIText(*obj);
			}
					
		}
	}

	App->physics->RenderTerrain(cam);
	if (has_render_tex)
	{
		cam->render_texture->Bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	map<float, GameObject*> alpha_objects;
	
	//Draw Static GO
	vector<GameObject*> static_objects;
	App->go_manager->octree.Intersect(static_objects, *cam); //Culling for static objects

	for (vector<GameObject*>::iterator obj = static_objects.begin(); obj != static_objects.end(); ++obj)
	{
		if ((*obj)->IsActive())
		{
			if (layer_mask == (layer_mask | (1 << (*obj)->layer)))
			{
				pair<float, GameObject*> alpha_object;
				Draw(*obj, App->lighting->GetLightInfo(), cam, alpha_object);
				if (alpha_object.second != nullptr)
				{
					alpha_objects.insert(alpha_object);
				}
			}
				
		}
	}

	//Draw dynamic GO
	for (vector<GameObject*>::const_iterator obj = objects_to_draw.begin(); obj != objects_to_draw.end(); ++obj)
	{
		if (cam->Intersects(*(*obj)->bounding_box))
		{
			if (layer_mask == (layer_mask | (1 << (*obj)->layer)))
			{
				pair<float, GameObject*> alpha_object;
				Draw(*obj, App->lighting->GetLightInfo(), cam, alpha_object);
				if (alpha_object.second != nullptr)
				{
					alpha_objects.insert(alpha_object);
				}
			}
		}
	}

	std::multimap<float, GameObject*>::reverse_iterator it = alpha_objects.rbegin();
	for (; it != alpha_objects.rend(); it++)
	{
		pair<float, GameObject*> alpha_object;
		Draw(it->second, App->lighting->GetLightInfo(),cam, alpha_object,true);
	}
	alpha_objects.clear();
	

	App->editor->skybox.Render(cam);

	if(has_render_tex)
		cam->render_texture->Unbind();
}


void ModuleRenderer3D::Draw(GameObject* obj, const LightInfo& light, ComponentCamera* cam, pair<float, GameObject*>& alpha_object, bool alpha_render) const
{

	ComponentMaterial* material = (ComponentMaterial*)obj->GetComponent(C_MATERIAL);

	if (material == nullptr)
		return;

	ComponentMesh* c_mesh = (ComponentMesh*)obj->GetComponent(C_MESH);
	if (c_mesh->HasBones())
	{
		DrawAnimated(obj, light, cam, alpha_object, alpha_render);
		return;
	}

	uint shader_id = 0;
	float4 color = { 1.0f,1.0f,1.0f,1.0f };
	color = float4(material->color);

	if (material->rc_material)
		shader_id = material->rc_material->GetShaderId();
	else
		shader_id = App->resource_manager->GetDefaultShaderId();

	bool ret_alpha = SetShaderAlpha(material, cam, obj, alpha_object, alpha_render);
	if (ret_alpha == false)
		return;
	
	//Use shader
	glUseProgram(shader_id);

	SetShaderUniforms(shader_id, obj, cam, material, light, color);

	//Buffer vertices == 0
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_vertices);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Buffer uvs == 1
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_uvs);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Buffer normals == 2
	glEnableVertexAttribArray(2);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_normals);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Buffer tangents == 3
	glEnableVertexAttribArray(3);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_tangents);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, obj->mesh_to_draw->id_indices);
	glDrawElements(GL_TRIANGLES, obj->mesh_to_draw->num_indices, GL_UNSIGNED_INT, (void*)0);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);

	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void ModuleRenderer3D::DrawAnimated(GameObject * obj, const LightInfo & light, ComponentCamera * cam, std::pair<float, GameObject*>& alpha_object, bool alpha_render)const
{
	ComponentMaterial* material = (ComponentMaterial*)obj->GetComponent(C_MATERIAL);

	if (material == nullptr)
		return;

	ComponentMesh* c_mesh = (ComponentMesh*)obj->GetComponent(C_MESH);

	float4 color = { 1.0f,1.0f,1.0f,1.0f };
	color = float4(material->color);

	uint shader_id = 0;
	if (material->rc_material)
		shader_id = material->rc_material->GetShaderId();
	else
		shader_id = App->resource_manager->GetDefaultAnimShaderId();

	bool ret_alpha = SetShaderAlpha(material, cam, obj, alpha_object, alpha_render);
	if (ret_alpha == false)
		return;

	//Use shader
	glUseProgram(shader_id);

	
	SetShaderUniforms(shader_id, obj, cam, material, light, color);

	//Array of bone transformations
	GLint bone_location = glGetUniformLocation(shader_id, "bones");
	glUniformMatrix4fv(bone_location, c_mesh->bones_trans.size(), GL_FALSE, reinterpret_cast<GLfloat*>(c_mesh->bones_trans.data()));


	//Buffer vertices == 0
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_vertices);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Buffer uvs == 1
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_uvs);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Buffer normals == 2
	glEnableVertexAttribArray(2);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_normals);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Buffer tangents == 3
	glEnableVertexAttribArray(3);
	glBindBuffer(GL_ARRAY_BUFFER, obj->mesh_to_draw->id_tangents);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
		

	//Buffer bones id == 4
	glEnableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, c_mesh->bone_id);
	glVertexAttribIPointer(4, 4, GL_INT, 0, (GLvoid*)0);

	//Buffer weights == 5
	glEnableVertexAttribArray(5);
	glBindBuffer(GL_ARRAY_BUFFER, c_mesh->weight_id);
	glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);

	//Index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, obj->mesh_to_draw->id_indices);
	glDrawElements(GL_TRIANGLES, obj->mesh_to_draw->num_indices, GL_UNSIGNED_INT, (void*)0);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(4);
	glDisableVertexAttribArray(5);

	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, 0);
}

bool ModuleRenderer3D::SetShaderAlpha(ComponentMaterial* material, ComponentCamera* cam, GameObject* obj, std::pair<float, GameObject*>& alpha_object, bool alpha_render) const
{
	if (material->alpha == 2 && alpha_render == false)
	{
		float distance = cam->GetProjectionMatrix().TranslatePart().Distance(obj->transform->GetPosition());
		alpha_object = pair<float, GameObject*>(distance, obj);
		return false;
	}

	switch (material->alpha)
	{
	case (2):
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, material->blend_type);
	}
	case (1):
	{
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, material->alpha_test);
		break;
	}
	}

	return true;
}

void ModuleRenderer3D::SetShaderUniforms(unsigned int shader_id, GameObject* obj, ComponentCamera* cam, ComponentMaterial* material, const LightInfo& light, const float4& color) const
{
	ShaderMVPUniforms(shader_id, obj, cam);

	ShaderTexturesUniforms(shader_id, material);

	ShaderLightUniforms(shader_id, light);

	ShaderCustomUniforms(shader_id, material);

	ShaderBuiltInUniforms(shader_id, cam, material, color);
}

void ModuleRenderer3D::ShaderMVPUniforms(unsigned int shader_id, GameObject* obj, ComponentCamera* cam) const
{
	GLint model_location = glGetUniformLocation(shader_id, "model");
	glUniformMatrix4fv(model_location, 1, GL_FALSE, *(obj->GetGlobalMatrix().Transposed()).v);
	GLint projection_location = glGetUniformLocation(shader_id, "projection");
	glUniformMatrix4fv(projection_location, 1, GL_FALSE, *cam->GetProjectionMatrix().v);
	GLint view_location = glGetUniformLocation(shader_id, "view");
	glUniformMatrix4fv(view_location, 1, GL_FALSE, *cam->GetViewMatrix().v);
}

void ModuleRenderer3D::ShaderTexturesUniforms(unsigned int shader_id, ComponentMaterial* material) const
{
	GLint alpha_location = glGetUniformLocation(shader_id, "_alpha_val");
	if (alpha_location != -1)
	{
		glUniform1f(alpha_location, material->alpha_test);
	}
	int count = 0;
	for (map<string, uint>::iterator tex = material->texture_ids.begin(); tex != material->texture_ids.end(); ++tex)
	{
		//Default first texture diffuse (if no specified)
		if ((*tex).first.compare("0") == 0 && count == 0 && (*tex).second != 0)
		{
			GLint has_tex_location = glGetUniformLocation(shader_id, "_HasTexture");
			glUniform1i(has_tex_location, 1);
			GLint texture_location = glGetUniformLocation(shader_id, "_Texture");
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, (*tex).second);
			glUniform1i(texture_location, 0);
			count++;
			continue;
		}

		//Default second texture normal (if no specified)
		if ((*tex).first.compare("1") == 0 && count == 1 && (*tex).second != 0)
		{
			GLint has_normal_location = glGetUniformLocation(shader_id, "_HasNormalMap");
			glUniform1i(has_normal_location, 1);
			GLint texture_location = glGetUniformLocation(shader_id, "_NormalMap");
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, (*tex).second);
			glUniform1i(texture_location, 1);
			count++;
			continue;
		}

		GLint tex_location = glGetUniformLocation(shader_id, (*tex).first.data());
		if (tex_location != -1)
		{
			glActiveTexture(GL_TEXTURE0 + count);
			glBindTexture(GL_TEXTURE_2D, (*tex).second);
			glUniform1i(tex_location, count);
			++count;
		}
	}

	//Reset Texture and Normal if doesn't have
	if (material->texture_ids.size() < 2)
	{
		GLint has_normal_location = glGetUniformLocation(shader_id, "_HasNormalMap");
		glUniform1i(has_normal_location, 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	if (material->texture_ids.empty() == true)
	{
		GLint has_tex_location = glGetUniformLocation(shader_id, "_HasTexture");
		glUniform1i(has_tex_location, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void ModuleRenderer3D::ShaderLightUniforms(unsigned int shader_id, const LightInfo& light) const
{
	//Ambient
	GLint ambient_intensity_location = glGetUniformLocation(shader_id, "_AmbientIntensity");
	if (ambient_intensity_location != -1)
		glUniform1f(ambient_intensity_location, light.ambient_intensity);
	GLint ambient_color_location = glGetUniformLocation(shader_id, "_AmbientColor");
	if (ambient_color_location != -1)
		glUniform3f(ambient_color_location, light.ambient_color.x, light.ambient_color.y, light.ambient_color.z);

	//Directional
	GLint has_directional_location = glGetUniformLocation(shader_id, "_HasDirectional");
	glUniform1i(has_directional_location, light.has_directional);

	if (light.has_directional)
	{
		GLint directional_intensity_location = glGetUniformLocation(shader_id, "_DirectionalIntensity");
		if (directional_intensity_location != -1)
			glUniform1f(directional_intensity_location, light.directional_intensity);
		GLint directional_color_location = glGetUniformLocation(shader_id, "_DirectionalColor");
		if (directional_color_location != -1)
			glUniform3f(directional_color_location, light.directional_color.x, light.directional_color.y, light.directional_color.z);
		GLint directional_direction_location = glGetUniformLocation(shader_id, "_DirectionalDirection");
		if (directional_direction_location != -1)
			glUniform3f(directional_direction_location, light.directional_direction.x, light.directional_direction.y, light.directional_direction.z);
	}
}

void ModuleRenderer3D::ShaderCustomUniforms(unsigned int shader_id, ComponentMaterial* material) const
{
	if (material->rc_material)
	{
		for (vector<Uniform*>::const_iterator uni = material->rc_material->material.uniforms.begin(); uni != material->rc_material->material.uniforms.end(); ++uni)
		{
			GLint uni_location = glGetUniformLocation(shader_id, (*uni)->name.data());

			if (uni_location != -1)
				switch ((*uni)->type)
				{
				case UniformType::U_BOOL:
				{
					glUniform1i(uni_location, *reinterpret_cast<bool*>((*uni)->value));
				}
				break;
				case U_INT:
				{
					glUniform1i(uni_location, *reinterpret_cast<int*>((*uni)->value));
				}
				break;
				case U_FLOAT:
				{
					glUniform1f(uni_location, *reinterpret_cast<GLfloat*>((*uni)->value));
				}
				break;
				case U_VEC2:
				{
					glUniform2fv(uni_location, 1, reinterpret_cast<GLfloat*>((*uni)->value));
				}
				break;
				case U_VEC3:
				{
					glUniform3fv(uni_location, 1, reinterpret_cast<GLfloat*>((*uni)->value));
				}
				break;
				case U_VEC4:
				{
					glUniform4fv(uni_location, 1, reinterpret_cast<GLfloat*>((*uni)->value));
				}
				break;
				case U_MAT4X4:
				{
					glUniformMatrix4fv(uni_location, 1, GL_FALSE, reinterpret_cast<GLfloat*>((*uni)->value));
				}
				break;
				case U_SAMPLER2D:
					//Already handled before.
					break;
				}
		}
	}
}

void ModuleRenderer3D::ShaderBuiltInUniforms(unsigned int shader_id, ComponentCamera* cam, ComponentMaterial* material, const float4 color) const
{
	//Time(special)
	GLint time_location = glGetUniformLocation(shader_id, "time");
	if (time_location != -1)
	{
		glUniform1f(time_location, time->RealTimeSinceStartup());
	}
	//Color
	GLint colorLoc = glGetUniformLocation(shader_id, "material_color");
	if (colorLoc != -1)
	{
		glUniform4fv(colorLoc, 1, color.ptr());
		if (material->rc_material != nullptr)
			material->rc_material->material.has_color = true;
	}
	//Specular
	GLint specular_location = glGetUniformLocation(shader_id, "_specular");
	if (specular_location != -1)
		glUniform1f(specular_location, material->specular);
	//EyeWorld
	GLint eye_world_pos = glGetUniformLocation(shader_id, "_EyeWorldPos");
	if (eye_world_pos != -1)
		glUniform3fv(eye_world_pos, 1, cam->GetPos().ptr());
}

void ModuleRenderer3D::DrawUIImage(GameObject * obj) const
{
	ComponentRectTransform* c = (ComponentRectTransform*)obj->GetComponent(C_RECT_TRANSFORM);
	

	ComponentUiImage* u = (ComponentUiImage*)obj->GetComponent(C_UI_IMAGE);
	ComponentUiButton* b;
	ComponentMaterial* m;
	if (u == nullptr)
	{
		b = (ComponentUiButton*)obj->GetComponent(C_UI_BUTTON);

		if (b == nullptr)
			return;
		else
			m = b->UImaterial;
	}
	else
		m = u->UImaterial;
	
	Mesh* mesh = c->GetMesh();
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glDisable(GL_LIGHTING); // Panel mesh is not afected by lights!

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, App->window->GetScreenWidth(), App->window->GetScreenHeight(), 0, -1, 1);	//
	glMatrixMode(GL_MODELVIEW);             // Select Modelview Matrix
	glPushMatrix();							// Push The Matrix
	glLoadIdentity();
	glMultMatrixf(*c->GetFinalTransform().Transposed().v);
	
	switch (m->alpha)
	{
	case (2):
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, m->blend_type);
	}
	case (1):
	{
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, m->alpha_test);
		break;
	}
	}

		// Vertices
		glBindBuffer(GL_ARRAY_BUFFER, mesh->id_vertices);
		glVertexPointer(3, GL_FLOAT, 0, NULL);
		// Texture coordinates
		glBindBuffer(GL_ARRAY_BUFFER, mesh->id_uvs);
		glTexCoordPointer(2, GL_FLOAT, 0, NULL);

		if (m->texture_ids.size()>0 )
		{
			// Texture
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindTexture(GL_TEXTURE_2D, (*m->texture_ids.begin()).second);
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glColor4fv(m->color);
		// Indices
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->id_indices);
		glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_INT, NULL);

	glMatrixMode(GL_PROJECTION);              // Select Projection
	glPopMatrix();							  // Pop The Matrix
	glMatrixMode(GL_MODELVIEW);               // Select Modelview
	glPopMatrix();							  // Pop The Matrix
	glPopMatrix();
	glEnable(GL_LIGHTING);
	
	glDisable(GL_TEXTURE_2D);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
}

void ModuleRenderer3D::DrawUIText(GameObject * obj) const
{
	BROFILER_CATEGORY("ModuleRenderer3D::DrawUIText", Profiler::Color::Teal);

	ComponentRectTransform* c = (ComponentRectTransform*)obj->GetComponent(C_RECT_TRANSFORM);

	ComponentUiText* t = (ComponentUiText*)obj->GetComponent(C_UI_TEXT);
	if (t == nullptr)
		return;
	
	if (t->UImaterial->texture_ids.size() <= 0)
		return;

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glDisable(GL_LIGHTING); // Panel mesh is not afected by lights!

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, App->window->GetScreenWidth(), App->window->GetScreenHeight(), 0, -1, 1);	//
	glMatrixMode(GL_MODELVIEW);             // Select Modelview Matrix
	glPushMatrix();							// Push The Matrix
	glLoadIdentity();

	switch (t->UImaterial->alpha)
	{
	case (2):
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, t->UImaterial->blend_type);
	}
	case (1):
	{
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, t->UImaterial->alpha_test);
		break;
	}
	}

	string text = t->GetText();
	string data_values = t->GetArrayValues();
	int row_chars = t->GetCharRows();
	float letter_w = c->GetRectSize().x;
	float letter_h = t->GetCharHeight();
	float2 pos = float2(c->GetLocalPos().ptr());
	float x = 0;
	float y = 0;
	Mesh* mesh = c->GetMesh();
	float4x4 m = c->GetFinalTransform();
	glMultMatrixf(*m.Transposed().v);
	float4x4 tmp = float4x4::identity;
	for (size_t i = 0; i < text.length(); i++)
	{
		for (uint j = 0; j < data_values.length(); ++j)
		{
			if (data_values[j] == text[i])
			{
				glMultMatrixf(*tmp.Transposed().v);
				if (t->UImaterial->texture_ids.size()>j)
				{
					// Texture
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, 0);
					glBindTexture(GL_TEXTURE_2D, (t->UImaterial->texture_ids.at(to_string(j))));
				}

				// Vertices
				glBindBuffer(GL_ARRAY_BUFFER, mesh->id_vertices);
				glVertexPointer(3, GL_FLOAT, 0, NULL);
				
				// Texture coordinates
				glBindBuffer(GL_ARRAY_BUFFER, mesh->id_uvs);
				glTexCoordPointer(2, GL_FLOAT, 0, NULL);

				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
				glColor4fv(t->UImaterial->color);
				// Indices
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->id_indices);
				glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_INT, NULL);
				
				tmp.SetTranslatePart(letter_w, 0.0f, 0.0f);
				x += letter_w;
				break;
			}
		}
	}
	
	glMatrixMode(GL_PROJECTION);              // Select Projection
	glPopMatrix();							  // Pop The Matrix
	glMatrixMode(GL_MODELVIEW);               // Select Modelview
	glPopMatrix();						  // Pop The Matrix
	glPopMatrix();
	for (size_t i = 0; i < text.length(); i++)
	{
		glPopMatrix();
	}
	glEnable(GL_LIGHTING);

	glDisable(GL_TEXTURE_2D);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
}

void ModuleRenderer3D::SetClearColor(const math::float3 & color) const
{
	glClearColor(color.x, color.y, color.z, 1.0f);
}

void ModuleRenderer3D::RemoveBuffer(unsigned int id)
{
	glDeleteBuffers(1, (GLuint*)&id);
	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
		LOG("Error removing buffer %i : %s", id, gluErrorString(error));
}

void ModuleRenderer3D::RemoveTextureBuffer(unsigned int id)
{
	glDeleteTextures(1, (GLuint*)&id);
	GLenum error = glGetError();
	if (error != GL_NO_ERROR)
		LOG("Error removing texture buffer %i : %s", id, gluErrorString(error));
}

void ModuleRenderer3D::DrawLine(float3 a, float3 b, float4 color)
{
	glDisable(GL_LIGHTING);

	glColor4f(color.x, color.y, color.z, color.w);
	glBegin(GL_LINES);
	glVertex3fv(a.ptr()); glVertex3fv(b.ptr());
	glEnd();

	glEnable(GL_LIGHTING);
}

void ModuleRenderer3D::DrawLocator(float4x4 transform, float4 color)
{
	if (App->IsGameRunning() == false)
	{
		glDisable(GL_LIGHTING);

		glPushMatrix();
		glMultMatrixf(transform.Transposed().ptr());

		glColor4f(color.x, color.y, color.z, color.w);

		glBegin(GL_LINES);

		glVertex3f(0.5f, 0.0f, 0.0f); glVertex3f(-0.5f, 0.0f, 0.0f);
		glVertex3f(0.0f, 0.5f, 0.0f); glVertex3f(0.0f, -0.5f, 0.0f);
		glVertex3f(0.0f, 0.0f, 0.5f); glVertex3f(0.0f, 0.0f, -0.5f);
		//Arrow indicating forward
		glVertex3f(0.0f, 0.0f, 0.5f); glVertex3f(0.1f, 0.0f, 0.4f);
		glVertex3f(0.0f, 0.0f, 0.5f); glVertex3f(-0.1f, 0.0f, 0.4f);

		glEnd();

		glEnable(GL_LIGHTING);

		glPopMatrix();
	}
}

void ModuleRenderer3D::DrawLocator(float3 pos, Quat rot, float4 color)
{
	if (App->IsGameRunning() == false)
	{
		DrawLocator(float4x4::FromTRS(pos, rot, float3(1, 1, 1)), color);
	}
}

void ModuleRenderer3D::DrawAABB(float3 minPoint, float3 maxPoint, float4 color)
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glLineWidth(1.5f);
	glColor4fv(color.ptr());
	glBegin(GL_QUADS);

	glNormal3f(0.0f, 0.0f, 1.0f);
	glVertex3f(minPoint.x, minPoint.y, maxPoint.z);
	glVertex3f(maxPoint.x, minPoint.y, maxPoint.z);
	glVertex3f(maxPoint.x, maxPoint.y, maxPoint.z);
	glVertex3f(minPoint.x, maxPoint.y, maxPoint.z);

	glNormal3f(0.0f, 0.0f, -1.0f);
	glVertex3f(maxPoint.x, minPoint.y, minPoint.z);
	glVertex3f(minPoint.x, minPoint.y, minPoint.z);
	glVertex3f(minPoint.x, maxPoint.y, minPoint.z);
	glVertex3f(maxPoint.x, maxPoint.y, minPoint.z);

	glNormal3f(1.0f, 0.0f, 0.0f);
	glVertex3f(maxPoint.x, minPoint.y, maxPoint.z);
	glVertex3f(maxPoint.x, minPoint.y, minPoint.z);
	glVertex3f(maxPoint.x, maxPoint.y, minPoint.z);
	glVertex3f(maxPoint.x, maxPoint.y, maxPoint.z);

	glNormal3f(-1.0f, 0.0f, 0.0f);
	glVertex3f(minPoint.x, minPoint.y, minPoint.z);
	glVertex3f(minPoint.x, minPoint.y, maxPoint.z);
	glVertex3f(minPoint.x, maxPoint.y, maxPoint.z);
	glVertex3f(minPoint.x, maxPoint.y, minPoint.z);

	glNormal3f(0.0f, 1.0f, 0.0f);
	glVertex3f(minPoint.x, maxPoint.y, maxPoint.z);
	glVertex3f(maxPoint.x, maxPoint.y, maxPoint.z);
	glVertex3f(maxPoint.x, maxPoint.y, minPoint.z);
	glVertex3f(minPoint.x, maxPoint.y, minPoint.z);

	glNormal3f(0.0f, -1.0f, 0.0f);
	glVertex3f(minPoint.x, minPoint.y, minPoint.z);
	glVertex3f(maxPoint.x, minPoint.y, minPoint.z);
	glVertex3f(maxPoint.x, minPoint.y, maxPoint.z);
	glVertex3f(minPoint.x, minPoint.y, maxPoint.z);

	glEnd();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}