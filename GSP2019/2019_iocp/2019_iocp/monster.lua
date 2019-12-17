my_id = 1;	

function set_npc_id(id)
	my_id = id
end

function  player_Conflict (player_id, x, y)
	my_x = API_get_x_position(my_id)
	my_y = API_get_y_position(my_id)
	if (x == my_x) then
	 if (y == my_y) then
		API_send_chat_packet(player_id, my_id, "Catch")
		return 2
		end
	end
	return 3
end



function event_monster_bye_notify (player_id)
	API_send_chat_packet(my_id, "Bye")
end

function init_set_npc_infor(player_id)

	init_x = (math.random(0,799)*my_id)%800
	init_y = (math.random(0,799)*my_id)%800
	init_type = (math.random(1,4)*my_id )%5
	init_level = (math.random(1,10))
	init_hp = 100+((100*init_level)/2)
	if (type == 2) then
		init_exp = init_level*5*2*2
	elseif (type == 3) then
		init_exp = init_level*5*2
	else
		init_exp = init_level*5
	end
	init_active = false;
	init_socket = -1;
	API_setting_Init_NPC(player_id, init_x, init_y, init_type, 
	init_level, init_hp, init_exp, init_active, init_socket)
end
