/**
 * ╔════════════════════════════════════════════════════════════════════════╗
 * ║                    BMini4 RC Car Remote Controller                     ║
 * ║                         Streamlined Version                            ║
 * ╚════════════════════════════════════════════════════════════════════════╝
 * 
 * OVERVIEW:
 * --------
 * Android app to control ESP8266-based RC car via WiFi. Uses accelerometer
 * for steering, touch pedals for gas/brake, and provides control for lights,
 * indicators, horn, and LED effects.
 * 
 * FEATURES:
 * ---------
 * • Tilt-based steering (45-135°)
 * • 5-level touch gas pedal
 * • Hold-to-brake with automatic reverse gear (after 1.2s)
 * • Headlights, turn indicators, horn
 * • 9 LED animation modes (off/blink/fade effects/on)
 * • Real-time connection monitoring via telemetry
 * • Haptic feedback
 * 
 * PROTOCOL:
 * ---------
 * Commands sent as plain text over TCP socket:
 *   servo <angle>    - Steering angle (45-135)
 *   gas <level>      - Throttle (0-5)
 *   brake on/off     - Brake control
 *   beep on/off      - Horn
 *   head on/off      - Headlights
 *   ind left/right/off - Turn indicators
 *   led <mode>       - LED effects
 * 
 * Telemetry received:
 *   TELEM:,,<battery> - Keep-alive message every 8s
 * 
 * CONNECTION:
 * -----------
 * • Default: 192.168.4.1:80 (ESP8266 AP mode)
 * • Auto-connects on startup
 * • Monitors connection via 9-second timeout
 * • Only sends changed values to reduce network traffic
 * 
 * OPTIMIZATION:
 * -------------
 * • Steering only sent when change >= 8° (reduces spam)
 * • Gas only sent on level change
 * • Single executor thread for all network ops
 * • Zero-allocation debug system
 * 
 * @author neuraknight
 * @date December 29, 2025
 * @license MIT
 */

// ═════════════════════════════════════════════════════════════════════════
// DEBUG SYSTEM
// ═════════════════════════════════════════════════════════════════════════

/**
 * Zero-overhead debug logging system with compile-time level control.
 * 
 * Usage:
 *   Debug.info("TAG", "Basic info message")
 *   Debug.debug("TAG", "Detailed debug message")
 *   Debug.verbose("TAG", "Everything including spam")
 * 
 * Change LEVEL to control output:
 *   NONE    = Production (no logs at all)
 *   INFO    = Connection events, mode changes
 *   DEBUG   = Commands, state changes
 *   VERBOSE = Every network operation
 */