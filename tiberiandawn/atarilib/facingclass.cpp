/*
 * facingclass.cpp - FacingClass implementation for Atari ST/MiNT
 */

#include "facing.h"

FacingClass::FacingClass(void)
{
	CurrentFacing = 0;
	DesiredFacing = 0;
}

int FacingClass::Set_Desired(DirType facing)
{
	DesiredFacing = facing;
	return 1;
}

int FacingClass::Set_Current(DirType facing)
{
	CurrentFacing = facing;
	return 1;
}

int FacingClass::Rotation_Adjust(int rate)
{
	if (CurrentFacing == DesiredFacing) {
		return 0;
	}
	
	int diff = Difference();
	if (diff > 128) {
		diff -= 256;
	} else if (diff < -128) {
		diff += 256;
	}
	
	if (diff > 0) {
		if (diff > rate) {
			CurrentFacing = (DirType)((CurrentFacing + rate) % 256);
		} else {
			CurrentFacing = DesiredFacing;
		}
	} else if (diff < 0) {
		if (-diff > rate) {
			CurrentFacing = (DirType)((CurrentFacing - rate + 256) % 256);
		} else {
			CurrentFacing = DesiredFacing;
		}
	}
	
	return (CurrentFacing != DesiredFacing);
}

