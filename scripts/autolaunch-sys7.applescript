on run argv
	set bootDelay to 12
	set openDelay to 1
	
	if (count of argv) is greater than or equal to 1 then
		set bootDelay to (item 1 of argv) as integer
	end if
	
	if (count of argv) is greater than or equal to 2 then
		set openDelay to (item 2 of argv) as integer
	end if
	
	tell application "System Events"
		repeat 60 times
			if exists process "BasiliskII" then exit repeat
			delay 1
		end repeat
		
		if not (exists process "BasiliskII") then error "BasiliskII did not appear"
		
		delay bootDelay
		
		tell process "BasiliskII"
			set frontmost to true
		end tell
		
		delay 1
		keystroke "b"
		delay 1
		keystroke "o" using command down
		delay openDelay
		keystroke "b"
		delay 1
		keystroke "o" using command down
	end tell
end run
