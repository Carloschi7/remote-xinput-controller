#include "wx_impl.hpp"
#include "incl.hpp"
#include "client.hpp"


namespace WX
{
	MainFrame::MainFrame(Components& components, const wxString& title) 
		: wxFrame(nullptr, wxID_ANY, title), comp(components)
	{
		this->SetSize(wxSize(1200, 800));
		wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

		comp.main_frame_panel = new wxPanel(this);
		comp.main_frame_static_text = new wxStaticText(comp.main_frame_panel, wxID_ANY,
			"Choose a room", wxPoint(250, 45), wxSize(100, 30));

		comp.main_frame_rooms_list_box = new wxListCtrl(comp.main_frame_panel, wxID_ANY, wxPoint(150,150),
			wxSize(400, 300), wxLC_REPORT);

		comp.main_frame_controllers_list_box = new wxListCtrl(comp.main_frame_panel, wxID_ANY, wxPoint(600, 150),
			wxSize(400, 300), wxLC_REPORT);

		//Available rooms          Room name          Users Connected          Max users
		Room::Info* rooms_data = nullptr;
		u32 rooms_count = 0;

		QueryRooms(comp.local_socket, &rooms_data, &rooms_count);

		auto* list_box = comp.main_frame_rooms_list_box;
		auto* controller_list_box = comp.main_frame_controllers_list_box;

		list_box->InsertColumn(0, "Room name", wxLIST_FORMAT_LEFT);
		list_box->InsertColumn(1, "Users connected", wxLIST_FORMAT_LEFT);
		list_box->InsertColumn(2, "Max users", wxLIST_FORMAT_LEFT);

		controller_list_box->InsertColumn(0, "Number");
		controller_list_box->InsertColumn(1, "Controller type");

		wxListItem& item = comp.main_frame_rooms_list_item;
		item.SetBackgroundColour(*wxRED);
		item.SetText(wxT("Programmer"));
		item.SetId(0);

		//wxButton& create_button = comp.main_frame_create_button;
		comp.main_frame_create_button = new wxButton(comp.main_frame_panel, wxID_ANY, "Create Room", wxPoint(150, 500), wxSize(100, 30));
		comp.main_frame_join_button = new wxButton(comp.main_frame_panel, wxID_ANY, "Join Room", wxPoint(300, 500), wxSize(100, 30));
		comp.main_frame_query_button = new wxButton(comp.main_frame_panel, wxID_ANY, "Query Rooms", wxPoint(450, 500), wxSize(100, 30));

		Bind(wxEVT_BUTTON, &MainFrame::JoinButtonCallback, this, comp.main_frame_join_button->GetId());
		Bind(wxEVT_BUTTON, &MainFrame::QueryButtonCallback, this, comp.main_frame_query_button->GetId());

		//Add rooms to UI
		for (u32 i = 0; i < rooms_count; i++) {
			list_box->InsertItem(i, item);

			char current_pads_char = '0' + rooms_data->current_pads;
			char max_pads_char = '0' + rooms_data->max_pads;

			list_box->SetItem(i, 0, rooms_data->name);
			list_box->SetItem(i, 1, current_pads_char);
			list_box->SetItem(i, 2, max_pads_char);
		}

		controller_list_box->InsertItem(0, item);
		controller_list_box->SetItem(0, 0, "0");
		controller_list_box->SetItem(0, 1, "Main Keyboard");

		//Find connection from xbox/dualshock pads
		s32* controller_handles = nullptr;
		u32 dualshock_controllers = QueryDualshockControllers(comp.fixed_buffer, &controller_handles);
		bool xbox_slots[4] = {};
		u32 xbox_controllers = QueryXboxControllers(xbox_slots);

		u32 partial_controller_count = 1;
		comp.dualshock_controllers_count = 0;
		comp.xbox_controllers_count = 0;

		for (u32 i = 0; i < dualshock_controllers; i++) {
			u32 current_index = partial_controller_count + i;
			controller_list_box->InsertItem(current_index, item);
			controller_list_box->SetItem(current_index, 0, std::to_string(current_index).c_str());
			std::string output = "Dualshock with handle " + std::to_string(controller_handles[i]);
			controller_list_box->SetItem(current_index, 1, output.c_str());
			comp.dualshock_controllers_count++;
		}

		partial_controller_count += dualshock_controllers;

		for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
			if (xbox_slots[i]) {
				u32 current_index = partial_controller_count + i;
				controller_list_box->InsertItem(current_index, item);
				controller_list_box->SetItem(current_index, 0, std::to_string(current_index).c_str());
				controller_list_box->SetItem(current_index, 1, "Xbox");

				partial_controller_count++;
				comp.xbox_controllers_count++;
			}
		}



		delete[] rooms_data;
	}

	void MainFrame::QueryButtonCallback(wxCommandEvent& event)
	{
		auto* list_box = comp.main_frame_rooms_list_box;

		Room::Info* rooms_data = nullptr;
		u32 rooms_count = 0;
		QueryRooms(comp.local_socket, &rooms_data, &rooms_count);

		list_box->DeleteAllItems();
		wxListItem& item = comp.main_frame_rooms_list_item;
		for (u32 i = 0; i < rooms_count; i++) {
			list_box->InsertItem(i, item);

			char current_pads_char = '0' + rooms_data->current_pads;
			char max_pads_char = '0' + rooms_data->max_pads;

			list_box->SetItem(i, 0, rooms_data->name);
			list_box->SetItem(i, 1, current_pads_char);
			list_box->SetItem(i, 2, max_pads_char);
		}

		delete[] rooms_data;
	}

	void MainFrame::CreateButtonCallback(wxCommandEvent& event)
	{
		//TODO
	}

	void MainFrame::JoinButtonCallback(wxCommandEvent& event)
	{
		s32 chosen_room = comp.main_frame_rooms_list_box->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		s32 chosen_controller = comp.main_frame_controllers_list_box->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (chosen_room == -1 || chosen_controller == -1) {
			wxMessageBox("Select a valid room and a valid controller", "Error", wxOK | wxICON_ERROR);
			return;
		}

		bool xbox_slots[4] = {};
		u32 xbox_controllers_count = QueryXboxControllers(xbox_slots);
		s32 dualshock_controllers_count = QueryDualshockCount();
		s32* controller_handles = nullptr;
		QueryDualshockControllers(comp.fixed_buffer, &controller_handles);

		if (xbox_controllers_count != comp.xbox_controllers_count && dualshock_controllers_count != comp.dualshock_controllers_count) {
			wxMessageBox("New devices have been plugged in, please refresh and make the selection again",
				"Error", wxOK | wxICON_ERROR);

			return;
		}

		ControllerType controller_type = CONTROLLER_TYPE_KEYBOARD;
		u32 controller_id = 0;
		const u32 keyboard_count = 1;

		if (chosen_controller == 0) {
			controller_type = CONTROLLER_TYPE_KEYBOARD;
			controller_id = 0;
		}
		else if (chosen_controller < keyboard_count + dualshock_controllers_count) {
			controller_type = CONTROLLER_TYPE_DUALSHOCK;
			controller_id = controller_handles[chosen_controller - keyboard_count];
		}
		else {
			controller_type = CONTROLLER_TYPE_XBOX;
			u32 xbox_single_count = chosen_controller - dualshock_controllers_count - keyboard_count + 1;
			for (u32 i = 0, j = 0; i < XUSER_MAX_COUNT && j < xbox_single_count; i++) {
				if (xbox_slots[i]) {
					controller_id = i;
					j++;
				}
			}
		}


		SendMsg(comp.local_socket, MESSAGE_REQUEST_ROOM_JOIN);
		Send(comp.local_socket, chosen_room);
		Message connection_status = ReceiveMsg(comp.local_socket);

		switch (connection_status) {
		case MESSAGE_ERROR_NONE: {
			comp.fixed_buffer.Init(FIXED_BUFFER_TYPE_CLIENT);
			//TODO add controller selection

			comp.exec_thread = SPAWN_THREAD(ExecuteClient(comp.fixed_buffer, comp.local_socket,
				controller_type, controller_id, false, &comp.exec_thread_flag));
		}break;
		case MESSAGE_ERROR_INDEX_OUT_OF_BOUNDS:
			wxMessageBox("Internal error, please refresh and try again", "Error", wxOK | wxICON_ERROR);
			break;
		case MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY:
			wxMessageBox("Room is at full capacity", "Error", wxOK | wxICON_ERROR);
			break;
		case MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD:
			wxMessageBox("Host had issues allocating a virtual pad, please try again", "Error", wxOK | wxICON_ERROR);
			break;
		}
	}
	


	ConnectionFrame::ConnectionFrame(Components& components, const wxString& title) 
		: wxFrame(nullptr, wxID_ANY, title), comp(components)
	{
		comp.connection_frame_panel = new wxPanel(this);
		this->SetSize(wxSize(450, 250));

		comp.connection_frame_text = new wxStaticText(comp.connection_frame_panel, wxID_ANY, 
			"Welcome to xinput-emu, insert the server IP to get started\n", wxPoint(100, 35), wxSize(100, 30));

		comp.connection_frame_text->Wrap(280);
		comp.connection_frame_button = new wxButton(comp.connection_frame_panel, wxID_ANY, "Connect", wxPoint(250, 95), wxSize(100, 30));
		comp.connection_frame_input_box = new wxTextCtrl(comp.connection_frame_panel, wxID_ANY, "", wxPoint(100, 100), wxSize(100, 20));


		Bind(wxEVT_BUTTON, &ConnectionFrame::ConnectButtonCallback, this, comp.connection_frame_button->GetId());
	}

	void ConnectionFrame::ConnectButtonCallback(wxCommandEvent&) 
	{
		comp.connection_frame_text->SetLabel("Connecting");

		auto label_str = comp.connection_frame_input_box->GetValue();
		const char* label = label_str.c_str();

		SOCKET ConnectToServer(const char* address, USHORT port);
		comp.local_socket = INVALID_SOCKET;
		comp.local_socket = ConnectToServer(label, 20000);

		if (comp.local_socket != INVALID_SOCKET) {
			comp.connection_frame->Hide();
			comp.main_frame = new MainFrame(comp, "Choose the room");
			comp.main_frame->Show();
		}
		else {
			comp.connection_frame_text->SetLabel("No server found at this IP or invalid IP, please try again");
		}
	}


	bool App::OnInit()
	{
		comp.connection_frame = new ConnectionFrame(comp, "First wx window");
		comp.connection_frame->Show();
		return true;
	}

	int EntryPoint()
	{
		return wxEntry();
	}

	App& wxGetApp()
	{
		return *static_cast<App*>(wxApp::GetInstance());
	}

	wxAppConsole* wxCreateApp()
	{
		wxAppConsole::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "Xinput-Emu");
		return new App;
	}
}


