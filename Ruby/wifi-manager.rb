require 'tk'

#refers to network ID in *wpa_supplicant* configuration (see +sudo wpa_cli list_networks+).
$NETWORK_ID = {AP1: 0, AP2: 1, AP3: 2, all: -2, none: -1}
$NETWORK_UI = {AP1: nil, AP2: nil, AP3: nil}

$info = nil

# Doesn't support multiple interface entries; it only expects entry '0'.
# Returns boolean or nil if error.
def get_rf_status
	result = nil
	
	rfStatus = `sudo rfkill -n -o ID,SOFT`
	$info.insert('end', "get_rf_status(): #{rfStatus}")
	
	if $?.success? && /^\s*(\d) (\w+)$/.match(rfStatus) && ($1.to_i == 0)
		case $2
		when 'blocked' then result = false
		when 'unblocked' then result = true
		end
	end
	
	return result
end

# Returns [<network ID = $NETWORK_ID>, <WPA state = {nil, boolean}>] array.
def get_wpa_status
	id = nil
	wpaState = nil
	
	wpaStatus = `sudo wpa_cli status`
	$info.insert('end', "get_wpa_status(): #{wpaStatus}")
	
	if $?.success?
		wpaStatus.each_line(chomp: true) {|line|					#parse output lines
			if (id == nil) && (/^id=(\d+)$/.match(line))
				case $+.to_i
				when $NETWORK_ID[:AP1], $NETWORK_ID[:AP2], $NETWORK_ID[:AP3] then id = $+.to_i
				else id = $NETWORK_ID[:none]
				end
			elsif (wpaState == nil) && (/^wpa_state=(\w+)$/.match(line))
				case $+
				when 'COMPLETED' then wpaState = true
				when 'INACTIVE' then wpaState = false
				when 'SCANNING'
					`sudo wpa_cli disable all`
					wpaState = false
				end
			end
		}
	end
	
	return [id, wpaState]
end

# Use +NETWORK_ID+ for +index+ value. Returns true if operation is successfully done.
def set_connection(index)
	result = true
	
	status = `sudo dhclient -r wlxaabbccddeeff`						#regardless of connection status
	if !$?.success?
		$info.insert('end', "set_connection(): Error releasing DHCP lease.\n")
		result = false
	end
	
	if result
		(id, wpaState) = get_wpa_status
		if wpaState && (id != $NETWORK_ID[:none])					#network still connected
			status = `sudo wpa_cli disable #{id}`
			$info.insert('end', "set_connection(): #{status}")
			
			if !$?.success?
				$info.insert('end', "set_connection(): Error disabling WPA network #{id}.\n")
				result = false
			end
		end
	end
	
	if result && (index != $NETWORK_ID[:none])
		status = `sudo wpa_cli select #{index}`
		$info.insert('end', "set_connection(): #{status}")
		
		if !$?.success?
			$info.insert('end', "set_connection(): Error selecting WPA network #{index}.\n")
			result = false
		else
			sleep 3													#wait for connection to establish
			
			(id, wpaState) = get_wpa_status
			if !wpaState || (id != index)
				$info.insert('end', "set_connection(): Error connecting to WPA network #{index}.\n")
				result = false
			end
		end
	end
	
	if result && (index != $NETWORK_ID[:none])
		`sudo dhclient -4 wlxaabbccddeeff`								#regardless of connection status
		if !$?.success?
			$info.insert('end', "set_connection(): Error obtaining DHCP lease.\n")
			result = false
		end
	end
	
	return result
end

# Use +NETWORK_ID+ for +index+ value.
def set_network_ui(index)
	case index
	when $NETWORK_ID[:AP1]
		$NETWORK_UI[:AP1]&.state = 'disabled'
		$NETWORK_UI[:AP2]&.state = 'normal'
		$NETWORK_UI[:AP3]&.state = 'normal'
	when $NETWORK_ID[:AP2]
		$NETWORK_UI[:AP1]&.state = 'normal'
		$NETWORK_UI[:AP2]&.state = 'disabled'
		$NETWORK_UI[:AP3]&.state = 'normal'
	when $NETWORK_ID[:AP3]
		$NETWORK_UI[:AP1]&.state = 'normal'
		$NETWORK_UI[:AP2]&.state = 'normal'
		$NETWORK_UI[:AP3]&.state = 'disabled'
	when $NETWORK_ID[:all]
		$NETWORK_UI[:AP1]&.state = 'normal'
		$NETWORK_UI[:AP2]&.state = 'normal'
		$NETWORK_UI[:AP3]&.state = 'normal'
	else
		$NETWORK_UI[:AP1]&.state = 'disabled'
		$NETWORK_UI[:AP2]&.state = 'disabled'
		$NETWORK_UI[:AP3]&.state = 'disabled'
	end
end

#UI components
#**************************************************************************************
ROOT = TkRoot.new {
	title("WiFi Connection Manager")
}
CTRL = TkFrame.new(ROOT) {
	bg('black')
	pack('fill' => 'both', 'side' => 'top')
}
INFO = TkFrame.new(ROOT) {
	bg('black')
	pack('fill' => 'both', 'side' => 'bottom')
}

$NETWORK_UI[:AP1] = TkButton.new(CTRL) {
	text("AP1")
	pack('side' => 'left', 'padx' => 10, 'pady' => 10)
	command() {
		set_network_ui(set_connection($NETWORK_ID[:AP1]) ? $NETWORK_ID[:AP1] : $NETWORK_ID[:all])
	}
}
$NETWORK_UI[:AP2] = TkButton.new(CTRL) {
	text("AP2")
	pack('side' => 'left', 'padx' => 10, 'pady' => 10)
	command() {
		set_network_ui(set_connection($NETWORK_ID[:AP2]) ? $NETWORK_ID[:AP2] : $NETWORK_ID[:all])
	}
}
$NETWORK_UI[:AP3] = TkButton.new(CTRL) {
	text("AP3")
	pack('side' => 'left', 'padx' => 10, 'pady' => 10)
	command() {
		set_network_ui(set_connection($NETWORK_ID[:AP3]) ? $NETWORK_ID[:AP3] : $NETWORK_ID[:all])
	}
}

INFO_SCROLL_H = TkScrollbar.new(INFO) {
	orient('horizontal')
	pack('fill' => 'x', 'side' => 'bottom')
}
INFO_SCROLL_V = TkScrollbar.new(INFO) {
	orient('vertical')
	pack('fill' => 'y', 'side' => 'right')
}
$info = TkText.new(INFO) {
	bg('black')
	fg('white')
	pack('fill' => 'both', 'side' => 'top')
	xscrollcommand(INFO_SCROLL_H.method(:set))
	yscrollcommand(INFO_SCROLL_V.method(:set))
	wrap('none')
}
INFO_SCROLL_H.configure('command' => $info&.method(:xview))
INFO_SCROLL_V.configure('command' => $info&.method(:yview))
#**************************************************************************************

#init UI
#**************************************************************************************
hasError = false

rfStatus = get_rf_status
if rfStatus == nil
	hasError = true
elsif rfStatus
	$info.insert('end', 'Interface 0 RF already unblocked.\n')
else																#if target interface RF is blocked
	`sudo rfkill unblock 0`
	if !$?.success?
		$info.insert('end', 'Error unblocking interface 0 RF.\n')
		hasError = true
	end
end

if !hasError
	(id, wpaState) = get_wpa_status
	if wpaState == nil
		hasError = true
	elsif wpaState													#network already connected
		case id
		when $NETWORK_ID[:AP1], $NETWORK_ID[:AP2], $NETWORK_ID[:AP3] then set_network_ui(id)
		else $info.insert('end', "Got unknown network ID: #{id}\n")
		end
	end
end

if hasError
	$info.insert('end', 'Initialization failed.\n')
	set_network_ui($NETWORK_ID[:none])
end
#**************************************************************************************

#cleanup
Tk::Wm::protocol(ROOT, 'WM_DELETE_WINDOW', -> {
	set_connection($NETWORK_ID[:none])

	if get_rf_status
		`sudo rfkill block 0`
		if !$?.success?
			$info.insert('end', 'Error blocking interface 0 RF.\n')
		end
	end
	
	ROOT.destroy
})
Tk.mainloop
