#include "wx_impl.hpp"
#include "incl.hpp"
#include "client.hpp"
#include "host.hpp"

namespace WX
{
	const u32 components_struct_size = sizeof(Components);
	const u32 connection_frame_class_size = sizeof(ConnectionFrame);
	const u32 main_frame_class_size = sizeof(MainFrame);
	const u32 room_creation_frame_class_size = sizeof(RoomCreationFrame);

	MainFrame::MainFrame(Core::FixedBuffer& wx_fixed_buffer, Components& components, const wxString& title)
		: wxFrame(nullptr, wxID_ANY, title), comp(components), wx_fixed_buffer(wx_fixed_buffer)
	{
		this->SetSize(wxSize(1200, 800));

		comp.main_frame_panel.Create(this);
		comp.main_frame_static_text.Create(&comp.main_frame_panel, wxID_ANY,
			"Choose a room", wxPoint(250, 45), wxSize(100, 30));

		comp.main_frame_rooms_list_box.Create(&comp.main_frame_panel, wxID_ANY, wxPoint(150, 150),
			wxSize(400, 300), wxLC_REPORT);

		comp.main_frame_controllers_list_box.Create(&comp.main_frame_panel, wxID_ANY, wxPoint(600, 150),
			wxSize(400, 300), wxLC_REPORT);

		//Available rooms          Room name          Users Connected          Max users
		Room::Info* rooms_data = nullptr;
		u32 rooms_count = 0;

		QueryRooms(comp.local_socket, &rooms_data, &rooms_count);

		auto& list_box = comp.main_frame_rooms_list_box;
		auto& controller_list_box = comp.main_frame_controllers_list_box;

		list_box.InsertColumn(0, "Room name", wxLIST_FORMAT_LEFT);
		list_box.InsertColumn(1, "Users connected", wxLIST_FORMAT_LEFT);
		list_box.InsertColumn(2, "Max users", wxLIST_FORMAT_LEFT);

		controller_list_box.InsertColumn(0, "Number");
		controller_list_box.InsertColumn(1, "Controller type");

		wxListItem& item = comp.main_frame_rooms_list_item;
		item.SetBackgroundColour(*wxRED);
		item.SetText(wxT("Programmer"));
		item.SetId(0);

		//wxButton& create_button = comp.main_frame_create_button;
		comp.main_frame_create_button.Create(&comp.main_frame_panel, wxID_ANY, "Create Room", wxPoint(150, 500), wxSize(100, 30));
		comp.main_frame_join_button.Create(&comp.main_frame_panel, wxID_ANY, "Join Room", wxPoint(300, 500), wxSize(100, 30));
		comp.main_frame_query_button.Create(&comp.main_frame_panel, wxID_ANY, "Query All", wxPoint(450, 500), wxSize(100, 30));

		Bind(wxEVT_BUTTON, &MainFrame::JoinButtonCallback, this, comp.main_frame_join_button.GetId());
		Bind(wxEVT_BUTTON, &MainFrame::QueryButtonCallback, this, comp.main_frame_query_button.GetId());
		Bind(wxEVT_BUTTON, &MainFrame::CreateButtonCallback, this, comp.main_frame_create_button.GetId());
		Bind(wxEVT_CLOSE_WINDOW, &MainFrame::CloseWindowCallback, this);

		MainFrameCompleteQuery(comp);
	}

	void MainFrame::QueryButtonCallback(wxCommandEvent& event)
	{
		MainFrameCompleteQuery(comp);
	}

	void MainFrame::CreateButtonCallback(wxCommandEvent& event)
	{
		comp.vigem_client = vigem_alloc();
		const auto connection = vigem_connect(comp.vigem_client);

		if (!VIGEM_SUCCESS(connection))
		{
			wxMessageBox("To run the server u need to have vigem installed", "Error", wxOK | wxICON_ERROR);
			VigemDeallocate(comp.vigem_client, nullptr, 0);
			return;
		}

		comp.main_frame_create_button.Disable();

		if (!comp.room_creation_frame) {
			void* room_creation_frame_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_ROOM_CREATION_FRAME);
			comp.room_creation_frame = new (room_creation_frame_buf) RoomCreationFrame(wx_fixed_buffer, comp, "Create Room");
		}
		else
			comp.room_creation_frame->Show();
	}

	void MainFrame::JoinButtonCallback(wxCommandEvent& event)
	{
		s32 chosen_room = comp.main_frame_rooms_list_box.GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		s32 chosen_controller = comp.main_frame_controllers_list_box.GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (chosen_room == -1 || chosen_controller == -1) {
			wxMessageBox("Select a valid room and a valid controller", "Error", wxOK | wxICON_ERROR);
			return;
		}

		ControllerData controller_data = QueryAllControllers();
		const u32& xbox_count = controller_data.xbox_count;
		const u32& dualshock_count = controller_data.dualshock_count;

		if (xbox_count != comp.xbox_controllers_count && dualshock_count != comp.dualshock_controllers_count) {
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
		else if (chosen_controller < keyboard_count + dualshock_count) {
			controller_type = CONTROLLER_TYPE_DUALSHOCK;
			controller_id = controller_data.dualshock_handles[chosen_controller - keyboard_count];
		}
		else {
			controller_type = CONTROLLER_TYPE_XBOX;
			u32 xbox_single_count = chosen_controller - dualshock_count - keyboard_count + 1;
			for (u32 i = 0, j = 0; i < XUSER_MAX_COUNT && j < xbox_single_count; i++) {
				if (controller_data.xbox_handles[i]) {
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


            //TODO implement ui elements that toggle the exec_thread_flag
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

	void MainFrame::CloseWindowCallback(wxCloseEvent&)
	{
		DestroyAppComponents(comp, wx_fixed_buffer);
	}

	RoomCreationFrame::RoomCreationFrame(Core::FixedBuffer& wx_fixed_buffer, Components& comp, const wxString& title)
		: wxFrame(nullptr, wxID_ANY, title), comp(comp), wx_fixed_buffer(wx_fixed_buffer)
	{
		this->SetSize(800, 600);
		comp.room_creation_panel.Create(this);

		comp.room_creation_max_controllers_combo_box.Create(&comp.room_creation_panel, wxID_ANY, "",
			wxPoint(100, 50), wxSize(100, 20), {"1", "2", "3", "4"}, wxCB_DROPDOWN | wxCB_READONLY);

		comp.room_creation_room_name_input_box.Create(&comp.room_creation_panel, wxID_ANY, "",
			wxPoint(480, 50), wxSize(120, 20));

		comp.room_creation_processes_list_box.Create(&comp.room_creation_panel, wxID_ANY,
			wxPoint(100, 100), wxSize(500, 300), wxLC_REPORT);

		auto& list_box = comp.room_creation_processes_list_box;
		list_box.InsertColumn(0, "Process ID", wxLIST_FORMAT_LEFT);
		list_box.InsertColumn(1, "Process name", wxLIST_FORMAT_LEFT);
		list_box.SetColumnWidth(1, 300);

		comp.room_creation_create_button.Create(&comp.room_creation_panel, wxID_ANY, "Create Room",
			wxPoint(100, 450), wxSize(100, 30));

        comp.room_creation_close_button.Create(&comp.room_creation_panel, wxID_ANY, "Close Room",
            wxPoint(300, 450), wxSize(100, 30));
        comp.room_creation_close_button.Disable();

		if (comp.fixed_buffer.Initialized()) {
			comp.fixed_buffer.ResetState();
		}
		comp.fixed_buffer.Init(FIXED_BUFFER_TYPE_HOST);
		comp.fixed_buffer.ResetMemory();

		Bind(wxEVT_SHOW, &RoomCreationFrame::ShowWindowCallback, this);
		Bind(wxEVT_CLOSE_WINDOW, &RoomCreationFrame::CloseWindowCallback, this);
		Bind(wxEVT_BUTTON, &RoomCreationFrame::CreateRoomCallback, this, comp.room_creation_create_button.GetId());
		Bind(wxEVT_BUTTON, &RoomCreationFrame::CloseRoomCallback, this, comp.room_creation_close_button.GetId());

		this->Show();
	}

	void RoomCreationFrame::ShowWindowCallback(wxShowEvent&)
	{
		auto enumeration = static_cast<WindowEnumeration*>(comp.fixed_buffer.GetHostSection(HOST_ALLOCATIONS_WINDOW_ENUM));
		EnumerateWindows(enumeration);

		auto& list_box = comp.room_creation_processes_list_box;
		list_box.DeleteAllItems();
		char current_window_name[max_window_name_length];

		for (u32 i = 0; i < enumeration->windows_count; i++) {
			list_box.InsertItem(i, comp.main_frame_rooms_list_item);
			list_box.SetItem(i, 0, std::to_string(i));

			std::memcpy(current_window_name, &enumeration->window_names[max_window_name_length * i],
				max_window_name_length);

			list_box.SetItem(i, 1, current_window_name);
		}

		comp.room_creation_room_name_input_box.Clear();
	}

	void RoomCreationFrame::CloseWindowCallback(wxCloseEvent&)
	{
	    //Closing the room creation window is allowed only if no room is being hosted
	    if(comp.exec_thread.get_id() != std::thread::id{}) return;

		comp.main_frame_create_button.Enable();
		this->Hide();
	}

	void RoomCreationFrame::CreateRoomCallback(wxCommandEvent&)
	{
        Room::Info info = {};
		info.current_pads = 0;
		wxString literal_value = comp.room_creation_max_controllers_combo_box.GetValue();
		XE_ASSERT(literal_value.size() == 1 && literal_value[0] >= '1' && literal_value[0] <= '4', "Value out of bounds");
		info.max_pads = literal_value[0] - '0';
        s32 item = 0;
        item = comp.room_creation_processes_list_box.GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

        wxString room_name = comp.room_creation_room_name_input_box.GetValue();
        std::memcpy(info.name, room_name.c_str(), room_name.size() + 1);

        wxString process_name_string = comp.room_creation_processes_list_box.GetItemText(item, 1);
        std::memcpy(comp.selected_window_name, process_name_string.c_str(), process_name_string.size() + 1);

        SendMsg(comp.local_socket, MESSAGE_REQUEST_ROOM_CREATE);
        Send(comp.local_socket, info);


        comp.room_creation_create_button.Disable();
        comp.room_creation_close_button.Enable();

        comp.exec_thread = SPAWN_THREAD(ExecuteHost(comp.fixed_buffer, comp.local_socket, comp.selected_window_name, info,
            comp.vigem_client, false, &comp.exec_thread_flag));
	}

    void RoomCreationFrame::CloseRoomCallback(wxCommandEvent&)
    {
        comp.exec_thread_flag = true;
        comp.exec_thread.join();
        comp.exec_thread_flag = false;

        comp.room_creation_close_button.Disable();
        comp.room_creation_create_button.Enable();
    }

	ConnectionFrame::ConnectionFrame(Core::FixedBuffer& wx_fixed_buffer, Components& components, const wxString& title)
		: wxFrame(nullptr, wxID_ANY, title), comp(components), wx_fixed_buffer(wx_fixed_buffer)
	{
		comp.connection_frame_panel.Create(this);
		this->SetSize(wxSize(450, 250));

		comp.connection_frame_text.Create(&comp.connection_frame_panel, wxID_ANY,
			"Welcome to xinput-emu, insert the server IP to get started\n", wxPoint(100, 35), wxSize(100, 30));

		comp.connection_frame_text.Wrap(280);
		comp.connection_frame_button.Create(&comp.connection_frame_panel, wxID_ANY, "Connect", wxPoint(250, 95), wxSize(100, 30));
		comp.connection_frame_input_box.Create(&comp.connection_frame_panel, wxID_ANY, "", wxPoint(100, 100), wxSize(100, 20));


		Bind(wxEVT_BUTTON, &ConnectionFrame::ConnectButtonCallback, this, comp.connection_frame_button.GetId());
		Bind(wxEVT_CLOSE_WINDOW, &ConnectionFrame::DestroyInstance, this);
	}

	void ConnectionFrame::ConnectButtonCallback(wxCommandEvent&)
	{
		comp.connection_frame_text.SetLabel("Connecting");

		auto label_str = comp.connection_frame_input_box.GetValue();
		const char* label = label_str.c_str();

		//SOCKET ConnectToServer(const char* address, USHORT port);
		comp.local_socket = INVALID_SOCKET;
		comp.local_socket = ConnectToServer(label, 20000);

		if (comp.local_socket != INVALID_SOCKET) {
			comp.connection_frame->Hide();
			void* main_frame_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_MAIN_FRAME);
			comp.main_frame = new (main_frame_buf) MainFrame(wx_fixed_buffer, comp, "Choose the room");
			comp.main_frame->Show();
		}
		else {
			comp.connection_frame_text.SetLabel("No server found at this IP or invalid IP, please try again");
		}
	}

	void ConnectionFrame::DestroyInstance(wxCloseEvent&)
	{
		DestroyAppComponents(comp, wx_fixed_buffer);
	}

	bool App::OnInit()
	{
		wx_fixed_buffer.Init(FIXED_BUFFER_TYPE_WX);
		wx_fixed_buffer.ResetMemory();

		//Init preallocated memory
		void* components_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_COMPONENTS);
		void* connection_frame_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_CONNECTION_FRAME);

		//Emplace new are only initializing the obj they do not allocate new memory
		comp = new (components_buf) Components;

		comp->connection_frame = new (connection_frame_buf) ConnectionFrame(wx_fixed_buffer, *comp, "First wx window");
		comp->connection_frame->Show();

		return true;
	}




	void MainFrameCompleteQuery(Components& comp)
	{
		auto& list_box = comp.main_frame_rooms_list_box;
		auto& controller_list_box = comp.main_frame_controllers_list_box;

		Room::Info* rooms_data = nullptr;
		u32 rooms_count = 0;
		QueryRooms(comp.local_socket, &rooms_data, &rooms_count);

		list_box.DeleteAllItems();
		wxListItem& item = comp.main_frame_rooms_list_item;
		for (u32 i = 0; i < rooms_count; i++) {
			list_box.InsertItem(i, item);

			char current_pads_char = '0' + rooms_data->current_pads;
			char max_pads_char = '0' + rooms_data->max_pads;

			list_box.SetItem(i, 0, rooms_data->name);
			list_box.SetItem(i, 1, current_pads_char);
			list_box.SetItem(i, 2, max_pads_char);
		}

		ControllerData controller_data = QueryAllControllers();
		const u32& xbox_count = controller_data.xbox_count;
		const u32& dualshock_count = controller_data.dualshock_count;

		controller_list_box.DeleteAllItems();
		controller_list_box.InsertItem(0, item);
		controller_list_box.SetItem(0, 0, "0");
		controller_list_box.SetItem(0, 1, "Main Keyboard");

		u32 partial_controller_count = 1;
		comp.dualshock_controllers_count = dualshock_count;
		comp.xbox_controllers_count = xbox_count;

		for (u32 i = 0; i < dualshock_count; i++) {
			u32 current_index = partial_controller_count + i;
			controller_list_box.InsertItem(current_index, item);
			controller_list_box.SetItem(current_index, 0, std::to_string(current_index).c_str());
			std::string output = "Dualshock with handle " + std::to_string(controller_data.dualshock_handles[i]);
			controller_list_box.SetItem(current_index, 1, output.c_str());
		}

		partial_controller_count += dualshock_count;

		for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
			if (controller_data.xbox_handles[i]) {
				u32 current_index = partial_controller_count + i;
				controller_list_box.InsertItem(current_index, item);
				controller_list_box.SetItem(current_index, 0, std::to_string(current_index).c_str());
				controller_list_box.SetItem(current_index, 1, "Xbox");

				partial_controller_count++;
			}
		}

		delete[] rooms_data;
	}

	void DestroyAppComponents(Components& comp, Core::FixedBuffer& wx_fixed_buffer)
	{
		//TODO is this deletion code valid?
		void* components_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_COMPONENTS);
		void* connection_frame_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_CONNECTION_FRAME);
		void* main_frame_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_MAIN_FRAME);
		void* room_creation_frame_buf = wx_fixed_buffer.GetWxSection(WX_ALLOCATIONS_ROOM_CREATION_FRAME);

		if (comp.connection_frame)
			static_cast<ConnectionFrame*>(connection_frame_buf)->Destroy();
		if (comp.main_frame)
			static_cast<MainFrame*>(main_frame_buf)->Destroy();
		if (comp.room_creation_frame)
			static_cast<RoomCreationFrame*>(room_creation_frame_buf)->Destroy();

		static_cast<Components*>(components_buf)->~Components();

		exit(0);
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


