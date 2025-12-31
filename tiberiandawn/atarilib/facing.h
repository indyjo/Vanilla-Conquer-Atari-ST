/*
 * facing.h - Facing calculation functions for Atari ST/MiNT
 * 
 * This header provides the function declarations for Desired_Facing256 and Desired_Facing8,
 * and the FacingClass definition.
 * 
 * Note: This file assumes DirType is defined elsewhere (typically in defines.h).
 * The root facing.h relies on DirType being defined by the time it's included.
 */

#ifndef ATARILIB_FACING_H
#define ATARILIB_FACING_H

#ifdef __cplusplus
extern "C" {
#endif

/* Desired facing calculation functions - always declare these */
int Desired_Facing256(long srcx, long srcy, long dstx, long dsty);
int Desired_Facing8(long x1, long y1, long x2, long y2);

#ifdef __cplusplus
}

// If the root facing.h is already included, don't redefine FacingClass
#ifndef FACING_H

/*
**	This is a general facing handler class. It is used in those cases where facing needs to be
**	kept track of, but there could also be an associated desired facing. The current facing
**	is supposed to transition to the desired state over time. Using this class facilitates this
**	processing as well as isolating the rest of the code from the internals.
*/
class FacingClass 
{
	public:
		FacingClass(void);
		FacingClass(DirType dir) {CurrentFacing = DesiredFacing = dir;};
		operator DirType(void) const {return CurrentFacing;};

		DirType Current(void) const {return CurrentFacing;};
		DirType Desired(void) const {return DesiredFacing;};

		int Set_Desired(DirType facing);
		int Set_Current(DirType facing);

		void Set(DirType facing) {
			Set_Current(facing);
			Set_Desired(facing);
		};

		DirType Get(void) const { return CurrentFacing; }

		int Is_Rotating(void) const {return (DesiredFacing != CurrentFacing);};

		int Difference(void) const {return (signed char)(*((unsigned char*)&DesiredFacing) - *((unsigned char*)&CurrentFacing));};
		int Difference(DirType facing) const {return (signed char)(*((signed char*)&facing) - *((signed char*)&CurrentFacing));};
		int Rotation_Adjust(int rate);

		// Assignment operator
		FacingClass& operator=(DirType dir) {
			Set(dir);
			return *this;
		}

		// Comparison operator
		bool operator==(DirType dir) const {
			return CurrentFacing == dir;
		}

		bool operator!=(DirType dir) const {
			return CurrentFacing != dir;
		}

	private:
		DirType CurrentFacing;
		DirType DesiredFacing;
};

#endif // __cplusplus

#endif // FACING_H (if root facing.h is not included)

#endif /* ATARILIB_FACING_H */

