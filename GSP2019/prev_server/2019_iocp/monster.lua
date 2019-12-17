my_id = 1;	

function set_npc_id(id)
	my_id = id
end

function  event_player_move_notify (player_id, x, y)
	my_x = API_get_x_position(my_id)
	my_y = API_get_y_position(my_id)
	if (x == my_x) then
	 if (y == my_y) then
		API_send_chat_packet(player_id, my_id, "Hello")
		return 2
		end
	end
	return 3
end

function event_monster_bye_notify (player_id)
	print("Bye")
	API_send_chat_packet(my_id, "Bye")
end

