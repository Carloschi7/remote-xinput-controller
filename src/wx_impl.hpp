#pragma once
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <thread>
#include "mem.hpp"


namespace WX
{
	class MainFrame;
	class ConnectionFrame;

	struct Components
	{
		SOCKET local_socket;
		std::thread exec_thread;
		std::atomic<bool> exec_thread_flag;
		Core::FixedBuffer fixed_buffer;
		u32 xbox_controllers_count;
		u32 dualshock_controllers_count;

		ConnectionFrame* connection_frame;
		wxPanel* connection_frame_panel;
		wxStaticText* connection_frame_text;
		wxButton* connection_frame_button;
		wxTextCtrl* connection_frame_input_box;

		MainFrame* main_frame;
		wxPanel* main_frame_panel;
		wxStaticText* main_frame_static_text;
		wxListCtrl* main_frame_rooms_list_box;
		wxListCtrl* main_frame_controllers_list_box;
		wxListItem main_frame_rooms_list_item;
		wxButton* main_frame_create_button;
		wxButton* main_frame_join_button;
		wxButton* main_frame_query_button;
	};

	class MainFrame : public wxFrame
	{
	public:
		MainFrame(Components& components, const wxString& title);

		void QueryButtonCallback(wxCommandEvent& event);
		void CreateButtonCallback(wxCommandEvent& event);
		void JoinButtonCallback(wxCommandEvent& event);

		Components& comp;
	};

	class ConnectionFrame : public wxFrame
	{
	public:
		ConnectionFrame(Components& components, const wxString& title);
		void ConnectButtonCallback(wxCommandEvent&);

		Components& comp;
	};

	class App : public wxApp {
	public:
		bool OnInit();

		Components comp = {};
	};

	int EntryPoint();
	App& wxGetApp();
	wxAppConsole* wxCreateApp();
	static wxAppInitializer wxTheAppInitializer((wxAppInitializerFunction)wxCreateApp);
}