/*
 * mouse.h - Mouse input header for Atari ST/MiNT
 * 
 * Provides function declarations for mouse input functions.
 */

#ifndef MOUSE_H
#define MOUSE_H

#ifdef __cplusplus
extern "C" {
#endif

// Mouse function declarations
int Get_Mouse_X(void);
int Get_Mouse_Y(void);

#ifdef __cplusplus
}
#endif

// Forward declarations (CELL and COORDINATE are typedefs, not classes)
class HouseClass;
class ObjectClass;
class CellClass;

#ifdef __cplusplus
// Include power.h so MouseClass can inherit from PowerClass
// The #ifndef POWER_H guard in power.h will prevent multiple inclusions
#include "power.h"

// Minimal MouseClass definition for use in main source
// This is used as a type alias in externs.h: extern MouseClass Map;
// MouseClass inherits from DisplayClass which inherits from MapClass
// So it needs all MapClass and DisplayClass methods
// Note: CELL and COORDINATE are typedefs defined in defines.h
// MouseClass also inherits from PowerClass (through ScrollClass->HelpClass->TabClass->SidebarClass->PowerClass)
class MouseClass : public PowerClass {
public:
	// ID method - returns cell number for a given pointer (from MapClass)
	template<class T> int ID(T const * ptr) const { return 0; } // Stub implementation
	template<class T> int ID(T const & ptr) const { return 0; } // Stub implementation
	
	// MapClass methods (CELL is a typedef, will be defined when includes are processed)
	bool In_Radar(int cell) const { return false; } // Stub - using int to avoid incomplete type
	void Sight_From(HouseClass *house, int cell, int sightrange, bool incremental=false) {} // Stub
	void Place_Down(int cell, ObjectClass * object) {} // Stub
	void Pick_Up(int cell, ObjectClass * object) {} // Stub (from MapClass)
	void Remove(ObjectClass * object, int layer) {} // Stub (from DisplayClass/LayerClass)
	void Submit(ObjectClass * object, int layer) {} // Stub (from DisplayClass/LayerClass)
	bool In_View(int cell) const { return false; } // Stub (from DisplayClass)
	void Flag_To_Redraw(bool complete=false) {} // Stub (from DisplayClass)
	void Refresh_Cells(int cell1, int cell2, bool and_for_allies=false) {} // Stub (from DisplayClass)
	
	// DisplayClass members that are accessed directly
	unsigned IsToRedraw:1; // Stub member from DisplayClass
	// Note: PowerClass::IsToRedraw is available through inheritance from PowerClass
	
	// DisplayClass methods
	// COORDINATE is a typedef (unsigned long), using unsigned long for now
	unsigned long Pixel_To_Coord(int x, int y) { return 0; } // Stub
	
	// DisplayClass/MapClass members for map cell dimensions
	int MapCellX;
	int MapCellY;
	int MapCellWidth;
	int MapCellHeight;
	
	// MapClass methods
	template<class T> void Add(T type, int id, bool something=false) {} // Stub
	void Recalc(void) {} // Stub
	
	// operator[] for cell access (from MapClass/GScreenClass)
	CellClass & operator[](int cell);
	CellClass const & operator[](int cell) const;
	
	// Static members from DisplayClass
	static unsigned char FadingShade[256];
	static unsigned char UnitShadow[256*256]; // Large enough for USHADOW_COL_COUNT
	static unsigned char WhiteTranslucentTable[256*2];
	static unsigned char TranslucentTable[256*256]; // Large enough for MAGIC_COL_COUNT
	static unsigned char RemapTables[8][3][256]; // HOUSE_COUNT * 3 * 256 (HOUSE_COUNT is typically 8)
	static void const *TransIconset;
	
	// MapClass methods
	// COORDINATE is a typedef (unsigned long), using unsigned long for now
	// Note: This needs to match the actual signature when COORDINATE is defined
	unsigned long Closest_Free_Spot(unsigned long coord, bool check_occupied=false) const { return coord; } // Stub
	
	// SidebarClass member - Column array (from SidebarClass)
	// Minimal StripClass definition for compatibility
	class StripClass {
	public:
		void Flag_To_Redraw(void) {} // Stub
		void Init_Clear(void) {} // Stub
		void AI(KeyNumType & input, int x, int y) {} // Stub
		void Set_Parent_Sidebar(void * parent) {} // Stub
	};
	StripClass Column[2]; // COLUMNS = 2
};

// WWMouseClass used in externs.h
class WWMouseClass {
	// Stub - needs implementation
};

#endif // __cplusplus

#endif /* MOUSE_H */
