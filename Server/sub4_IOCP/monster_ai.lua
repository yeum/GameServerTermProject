myid = 9999;
myx = 0;
myy = 0;
mylevel = 0;
myhp = 0;
myobj_class = 0;
mytype = 0;
myexp = 0;
myatk = 0;
detect_range = 10;

function init(uid, x, y, level, hp, exp, atk, class, type)
	myid = uid;
	myx = x;
	myy = y;
	mylevel = level;
	myhp = hp;
	myexp = exp;
	myatk = atk;
	myclass = class;
	mytype = type;
end

function set_uid(x)
	myid = x;
end

function set_pos(x, y)
	myx = x;
	myy = y;
end

function set_level(level)
	mylevel = level;
end

function set_hp(hp)
	myhp = hp;
end

function set_exp(exp)
	myexp = exp;
end

function set_atk(atk)
	myatk = atk;
end

function set_obj_class(class)
	myobj_class = class;
end

function set_type( type )
	mytype = type;
end

function get_type()
	return mytype;
end

function meet_client(player)
	API_SetIsAttack(player, myid);
end

function event_player_move(player)
	player_x = API_get_x(player);
	player_y = API_get_y(player);

	if(player_x <= myx + detect_range/2 and player_x > myx - detect_range/2) then
		if(player_y <= myy + detect_range/2 and player_y > myy - detect_range/2) then
			meet_client(player);
		end
	end
end
