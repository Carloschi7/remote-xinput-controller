#pragma once
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <thread>
#include "mem.hpp"
#include "incl.hpp"

namespace WX
{
	class MainFrame;
	class ConnectionFrame;
	class RoomCreationFrame;

	struct Components
	{
		//Internals that interface with the system
		struct {
			SOCKET local_socket;
			std::thread exec_thread;
			std::atomic<bool> exec_thread_flag;
			Core::FixedBuffer fixed_buffer;
			u32 xbox_controllers_count;
			u32 dualshock_controllers_count;
			PVIGEM_CLIENT vigem_client;
		};

		ConnectionFrame* connection_frame = nullptr;
		wxPanel connection_frame_panel;
		wxStaticText connection_frame_text;
		wxButton connection_frame_button;
		wxTextCtrl connection_frame_input_box;

		MainFrame* main_frame = nullptr;
		wxPanel main_frame_panel;
		wxStaticText main_frame_static_text;
		wxListCtrl main_frame_rooms_list_box;
		wxListCtrl main_frame_controllers_list_box;
		wxListItem main_frame_rooms_list_item;
		wxButton main_frame_create_button;
		wxButton main_frame_join_button;
		wxButton main_frame_query_button;

		RoomCreationFrame* room_creation_frame = nullptr;
		wxPanel room_creation_panel;
		wxComboBox room_creation_max_controllers_combo_box;
		wxTextCtrl room_creation_room_name_input_box;
		wxListCtrl room_creation_processes_list_box;
		wxButton room_creation_create_button;
	};

	//Not a big fan of this OOP galore but i guess there is no other sane way of implementing this UI

	class MainFrame : public wxFrame
	{
	public:
		MainFrame(Core::FixedBuffer& wx_fixed_buffer, Components& components, const wxString& title);

		void QueryButtonCallback(wxCommandEvent& event);
		void CreateButtonCallback(wxCommandEvent& event);
		void JoinButtonCallback(wxCommandEvent& event);
		void CloseWindowCallback(wxCloseEvent&);

		Components& comp;
		Core::FixedBuffer& wx_fixed_buffer;
	};

	class RoomCreationFrame : public wxFrame
	{
	public:
		RoomCreationFrame(Core::FixedBuffer& wx_fixed_buffer, Components& comp, const wxString& title);
		void ShowWindowCallback(wxShowEvent&);
		void CloseWindowCallback(wxCloseEvent&);

		void CreateRoomCallback(wxCommandEvent&);

		Components& comp;
		Core::FixedBuffer& wx_fixed_buffer;
	};

	class ConnectionFrame : public wxFrame
	{
	public:
		ConnectionFrame(Core::FixedBuffer& wx_fixed_buffer, Components& components, const wxString& title);
		void ConnectButtonCallback(wxCommandEvent&);
		void DestroyInstance(wxCloseEvent&);

		Components& comp;
		Core::FixedBuffer& wx_fixed_buffer;
	};

	class App : public wxApp {
	public:
		bool OnInit();

		Components* comp = nullptr;
		Core::FixedBuffer wx_fixed_buffer;
	};


	void MainFrameCompleteQuery(Components& comp);
	void DestroyAppComponents(Components& comp, Core::FixedBuffer& wx_fixed_buffer);

	int EntryPoint();
	App& wxGetApp();
	wxAppConsole* wxCreateApp();
	static wxAppInitializer wxTheAppInitializer((wxAppInitializerFunction)wxCreateApp);
}