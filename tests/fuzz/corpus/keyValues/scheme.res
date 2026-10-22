//
// A scheme shaped like VGUI2's TrackerScheme.res, written for the fuzzer
//
Scheme
{
	Colors
	{
		"BaseText"			"216 222 211 255"	// used in text windows, lists
		"WindowBG"			"62 70 55 255"
	}
	BaseSettings
	{
		"FgColor"			"BaseText"
		"Font"				"Default"		[$WIN32]
	}
	Fonts
	{
		"Default"
		{
			"1"
			{
				"name"		"Tahoma"
				"tall"		"16"
				"weight"	"0"
			}
		}
	}
	Borders
	{
		BaseBorder		"InsetBorder"
		InsetBorder
		{
			"inset" "0 0 1 1"
			Left { "1" { "color" "BorderDark" "offset" "0 1" } }
		}
	}
}
