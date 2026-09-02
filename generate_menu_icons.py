#!/usr/bin/env python3
"""
Generate simple 48x48 4-bit grayscale icons for menu categories.
Each icon is 1152 bytes (48 * 48 / 2 pixels, 4-bit per pixel).
"""

def create_solid_icon(color_nibble=0xF, text_label=""):
    """Create a solid colored 48x48 icon."""
    icon = bytearray(1152)
    
    # Fill entire icon with the color
    for i in range(1152):
        # Each byte contains 2 pixels, pack them together
        icon[i] = (color_nibble << 4) | color_nibble
    
    return bytes(icon)

def create_utilities_icon():
    """
    Create icon for Utilities menu (gear/cog-like pattern).
    Uses gray (0xA) as main color, darker areas (0x5).
    """
    icon = bytearray(1152)
    
    # Fill with light gray background
    for i in range(1152):
        icon[i] = 0xFF  # White background (F F)
    
    # Create a simple gear pattern - draw circle/gear outline
    center_x, center_y = 24, 24
    
    for y in range(48):
        for x in range(48):
            pixel_idx = y * 24 + x // 2
            
            # Calculate distance from center
            dx = x - center_x
            dy = y - center_y
            dist = (dx*dx + dy*dy) ** 0.5
            
            # Draw gear teeth pattern (alternating segments)
            angle = abs(dx) + abs(dy)  # Simple angular approximation
            
            if 15 <= dist <= 18:  # Outer ring
                color = 0x2  # Dark
            elif 10 <= dist <= 12:  # Inner ring
                color = 0x2  # Dark
            elif 6 <= dist <= 9:  # Middle ring
                color = 0xA  # Gray
            else:
                color = 0xF  # White
            
            # Pack two pixels per byte
            if x % 2 == 0:
                icon[pixel_idx] = (icon[pixel_idx] & 0x0F) | (color << 4)
            else:
                icon[pixel_idx] = (icon[pixel_idx] & 0xF0) | color
    
    return bytes(icon)

def create_games_icon():
    """
    Create icon for Games menu (game controller/sword-like pattern).
    Uses bright color (0xC) for main elements.
    """
    icon = bytearray(1152)
    
    # Fill with light gray background
    for i in range(1152):
        icon[i] = 0xFF  # White background (F F)
    
    center_x, center_y = 24, 24
    
    for y in range(48):
        for x in range(48):
            pixel_idx = y * 24 + x // 2
            
            # Create sword-like shape
            dx = x - center_x
            dy = y - center_y
            
            # Vertical blade
            if -6 <= dx <= 6 and -20 <= dy <= 20:
                color = 0xC  # Bright color
            # Horizontal cross-guard
            elif -10 <= dx <= 10 and -4 <= dy <= 4:
                color = 0x8  # Medium color
            # Handle
            elif -4 <= dx <= 4 and 18 <= dy <= 24:
                color = 0x4  # Dark color
            else:
                color = 0xF  # White background
            
            # Pack two pixels per byte
            if x % 2 == 0:
                icon[pixel_idx] = (icon[pixel_idx] & 0x0F) | (color << 4)
            else:
                icon[pixel_idx] = (icon[pixel_idx] & 0xF0) | color
    
    return bytes(icon)

def main():
    # Generate icons
    utilities_icon = create_utilities_icon()
    games_icon = create_games_icon()
    
    # Output as C array
    print("// New menu category icons (2 icons * 1152 bytes = 2304 bytes)")
    print("static const uint8_t g_menuCategoryIcons[2304] = {")
    
    # Utilities icon
    print("// Icon 0: Utilities (gear)")
    hex_strings = []
    for i, byte in enumerate(utilities_icon):
        hex_strings.append(f"0x{byte:02X}")
        if (i + 1) % 16 == 0:
            print(", ".join(hex_strings) + ",")
            hex_strings = []
    
    if hex_strings:
        print(", ".join(hex_strings) + ",")
    
    # Games icon
    print("\n// Icon 1: Games (sword)")
    hex_strings = []
    for i, byte in enumerate(games_icon):
        hex_strings.append(f"0x{byte:02X}")
        if (i + 1) % 16 == 0:
            print(", ".join(hex_strings) + ",")
            hex_strings = []
    
    if hex_strings:
        print(", ".join(hex_strings))
    
    print("};")

if __name__ == "__main__":
    main()
