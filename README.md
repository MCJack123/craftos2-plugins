# craftos2-plugins
This repository contains a number of small plugins for CraftOS-PC. These were previously in separate Gists, but were moved here to consolidate them into one repository as well as to provide built versions.

You can download pre-built versions of these plugins for Windows in `x64-windows`.

## ccemux
The CCEmuX plugin as included with CraftOS-PC. This repo will only contain a mirror of the original file in the CraftOS-PC repo.

## discord
Discord Rich Presence for CraftOS-PC.

### Installation
Drop the plugin file into the `plugins` directory. In addition, download [the Discord GameSDK](https://dl-game-sdk.discordapp.net/2.5.6/discord_game_sdk.zip), and copy these files depending on your OS:
* Windows: `lib/x86_64/discord_game_sdk.dll` => `CraftOS-PC\discord_game_sdk.dll`
* macOS: `lib/x86_64/discord_game_sdk.dylib` => `CraftOS-PC.app/Contents/Frameworks/discord_game_sdk.dylib`
* Linux: `lib/x86_64/discord_game_sdk.so` => `/usr/lib/discord_game_sdk.so`

### Usage
Just launch CraftOS-PC, and rich presence will automatically enable. If you use cash, you'll need to run `rom/autorun/discord.lua` to enable it.

## joystick
Adds the ability to use joysticks and gamepads with CraftOS-PC.

### Installation
Just drop the plugin file into `plugins`.

### API
To use a joystick, call `joystick.open(id)`. This will return a new joystick handle, and enables event reporting for the selected joystick.

* *number* count(): Returns the number of joysticks currently attached.
* *handle* open(*number* id): Opens a joystick.
  * id: The ID of the joystick to open. This ranges from 0 to `joystick.count()-1`.
  * Returns: A new joystick handle, or `nil` + an error message on error.

#### Joystick handle
* *void* close(): Closes the handle.
* *number* getAxis(*number* id): Returns the position of the selected axis.
  * id: The ID of the axis to check.
  * Returns: The position of the selected axis, from -1.0 to 1.0.
* *number*, *number* getBall(*number* id): Returns the coordinates of a ball input.
  * id: The ID of the ball to check.
  * Returns: The X and Y coordinates of the ball.
* *boolean* getButton(*number* id): Returns whether a button is pressed.
  * id: The ID of the button to check.
  * Returns: Whether the button is pressed.
* *number*, *number* getHat(*number* id): Returns the position of a hat input.
  * id: The ID of the hat to check.
  * Returns: Two numbers declaring the position of the hat:
    * -1 for left, 0 for center, 1 for right
    * -1 for down, 0 for center, 1 for up
* *string* getPowerLevel(): Returns the power level of the controller if available.
  * Possible values: nil, `empty`, `low`, `medium`, `full`, `max`, `wired`
* *boolean* rumble(*number* time, *number* lowIntensity, *number* highIntensity): Rumbles the controller if available. (Only if built with SDL 2.0.9 or later.)
  * time: The amount of time to rumble the controller, in seconds.
  * lowIntensity: The intensity to rumble the low frequency motor at, from 0.0 to 1.0.
  * highIntensity: The intensity to rumble the high frequency motor at, from 0.0 to 1.0.
  * Returns: `true` if the rumble succeeded, `false` if rumble is not supported.
* *number* id: The ID of the controller.
* *string* name: The name of the controller.
* *number* player: The player number of the controller. (Only if built with SDL 2.0.9 or later.)
* *number* axes: The number of axes available.
* *number* balls: The number of balls available.
* *number* buttons: The number of buttons available.
* *number* hats: The number of hats available.

#### Events
* joystick: Fired when a joystick is attached.
  * *number* joy: The ID of the new joystick.
* joystick_detach: Fired when a joystick is detached.
  * *number* joy: The ID of the removed joystick.
* joystick_press: Fired when a button on a joystick is pressed.
  * *number* joy: The ID of the joystick the event was fired for.
  * *number* id: The ID of the button pressed.
* joystick_up: Fired when a button on a joystick is released.
  * *number* joy: The ID of the joystick the event was fired for.
  * *number* id: The ID of the button released.
* joystick_axis: Fired when an axis on a joystick changes.
  * *number* joy: The ID of the joystick the event was fired for.
  * *number* id: The ID of the axis that changed.
  * *number* pos: The position of the axis, from -1.0 to 1.0.
* joystick_ball: Fired when a ball on a joystick changes.
  * *number* joy: The ID of the joystick the event was fired for.
  * *number* id: The ID of the ball that changed.
  * *number* x: The X position of the ball.
  * *number* y: The Y position of the ball.
* joystick_hat: Fired when a hat on a joystick changes.
  * *number* joy: The ID of the joystick the event was fired for.
  * *number* id: The ID of the hat that changed.
  * *number* x: -1 if left, 0 if center, 1 if right
  * *number* y: -1 if down, 0 if center, 1 if up

## sound
Adds a number of programmable sound channels (default 4) that play sound waves with the specified frequency, wave type, volume, and pan position.

### Installation
Just drop the plugin file into `plugins`.

### Configuration
* *number* sound.numChannels: The number of channels available. Defaults to 4.

### API
The `sound` API contains all the functions required to operate the sound generator.

* *string* getWaveType(*number* channel): Returns the type of wave used on a channel.
  * channel: The channel to check.
  * Returns: `none` for off, `sine` for sine, `triangle` for triangle, `sawtooth` for sawtooth, `rsawtooth` for reversed sawtooth, `square` for square, or `noise` for noise.
* *number* getFrequency(*number* channel): Returns the current frequency set on a channel.
  * channel: The channel to check.
  * Returns: The frequency for the channel, in Hertz.
* *number* getVolume(*number* channel): Returns the current volume set on a channel.
  * channel: The channel to check.
  * Returns: The volume for the channel, from 0.0 to 1.0.
* *number* getPan(*number* channel): Returns the current pan set on a channel.
  * channel: The channel to check.
  * Returns: The pan for the channel, from -1.0 (right) to 1.0 (left).
* *void* setWaveType(*number* channel, *string* waveType): Sets the type of wave used on a channel.
  * channel: The channel to set.
  * Returns: `none` for off, `sine` for sine, `triangle` for triangle, `sawtooth` for sawtooth, `rsawtooth` for reversed sawtooth, `square` for square, or `noise` for noise.
* *void* setFrequency(*number* channel, *number* frequency): Sets the current frequency set on a channel.
  * channel: The channel to set.
  * frequency: The frequency for the channel, in Hertz.
* *void* setVolume(*number* channel, *number* volume): Sets the current volume set on a channel.
  * channel: The channel to set.
  * volume: The volume for the channel, from 0.0 to 1.0.
* *void* setPan(*number* channel, *number* pan): Sets the current pan set on a channel.
  * channel: The channel to set.
  * pan: The pan for the channel, from -1.0 (right) to 1.0 (left).
* *void* fadeOut(*number* channel, *number* time): Fades out a channel over time.
  * channel: The channel to fade out.
  * time: The time to fade out for, in seconds. Set to 0 to stop any active fade out operation.
