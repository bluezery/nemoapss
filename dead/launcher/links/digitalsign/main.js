const {BrowserWindow, app} = require('electron')
const config = require('./config.json');

// Keep a global reference of the window object, if you don't, the window will
// be closed automatically when the JavaScript object is garbage collected.
app.commandLine.appendSwitch('--touch-events');
app.commandLine.appendSwitch('--ignore-certificate-errors');
app.commandLine.appendSwitch('--ppapi-flash-path', '/usr/lib/libpepflashplayer.so');
//app.disableHardwareAcceleration();

let mainWindow

function createWindow () {
  // Create the browser window.
  mainWindow = new BrowserWindow({
		width: config.width,
		height: config.height,
		frame: false,
		fullscreenable: false,
		plugins: true,
		webPreferences: {
			nodeIntegration: false,
			//offscreen: true
		}
	});

	let webContents = mainWindow.webContents;

	/*
	webContents.on('new-window', (event, url) => {
		const protocol = require('url').parse(url).protocol;
		if (protocol === 'http:' || protocol === 'https:') {
			event.preventDefault()
			console.log('navigate other url in place');
			webContents.loadURL(url);
		}
	});
	*/

	//webContents.setUserAgent(ua);

  // and load the index.html of the app.
  mainWindow.loadURL(config.url);

  // Open the DevTools.
  //mainWindow.webContents.openDevTools()

  // Emitted when the window is closed.
  mainWindow.on('closed', function () {
    // Dereference the window object, usually you would store windows
    // in an array if your app supports multi windows, this is the time
    // when you should delete the corresponding element.
    mainWindow = null
  })

	mainWindow.on('resize', function () {
		//console.log('win bound object: ', mainWindow.getContentBounds());
		//let size = mainWindow.getBounds();
		//mainWindow.setContentSize(size.width, size.height);
	})
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', createWindow)

// Quit when all windows are closed.
app.on('window-all-closed', function () {
  // On OS X it is common for applications and their menu bar
  // to stay active until the user quits explicitly with Cmd + Q
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

app.on('activate', function () {
  // On OS X it's common to re-create a window in the app when the
  // dock icon is clicked and there are no other windows open.
  if (mainWindow === null) {
    createWindow()
  }
})
// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and require them here.
