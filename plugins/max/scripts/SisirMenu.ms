-- Sisir for 3ds Max: the Sisir menu, run before 3ds Max starts (PackageContents.xml).
--
-- The actions are macroscripts (category "Sisir"), so they can also go on toolbars, quad menus and
-- hotkeys. They call the plug-in's `sisir` interface (SisirInterface.cpp). The menu is built fresh
-- at every start and removed first, so an uninstalled plug-in leaves no dead menu behind:
-- 3ds Max 2022-2024 through menuMan, 2025 and later through the CUI menu system.

macroScript Sisir_ImportScene category:"Sisir" buttonText:"Import Sisir Scene..." tooltip:"Bake and import a Sisir .sir scene"
(
	if sisir != undefined then sisir.importBrowse() else messageBox "Sisir for 3ds Max is not loaded." title:"Sisir"
)

macroScript Sisir_ExportHairAbc category:"Sisir" buttonText:"Export Hair (ABC)..." tooltip:"The selected Sisir hair (and the visible hair in selected groups) as an Alembic groom laid out like Sisir's own export"
(
	if sisir != undefined then sisir.exportAbcBrowse() else messageBox "Sisir for 3ds Max is not loaded." title:"Sisir"
)

macroScript Sisir_ExportGuidesPly category:"Sisir" buttonText:"Export Guides (PLY)..." tooltip:"The selected splines (and the visible splines in selected groups) as Sisir guides"
(
	if sisir != undefined then sisir.exportGuidesPly() else messageBox "Sisir for 3ds Max is not loaded." title:"Sisir"
)

(
	if (maxVersion())[1] >= 27000 then (
		-- 3ds Max 2025+: menus are registered when Max asks for them.
		global sisirRegisterMenus
		fn sisirRegisterMenus = (
			local menuMgr = callbacks.notificationParam()
			local mainMenuBar = menuMgr.mainMenuBar
			local menu = mainMenuBar.CreateSubMenu "5b1d64f2-7d3a-4c1e-9a52-0c7f3e2b8d41" "Sisir"
			menu.CreateAction "8e0a3c7d-1f24-4b6e-b3a9-6d2c5e4f7a10" 647394 "Sisir_ImportScene`Sisir"
			menu.CreateSeparator "2c6e9f13-4a8b-4d7c-8e15-3b9a0d2f6c57"
			menu.CreateAction "a47d2e90-6b31-4f8c-9d02-5e1c7b3a8f64" 647394 "Sisir_ExportHairAbc`Sisir"
			menu.CreateAction "d93b5f28-0e6c-4a17-b4d8-7f2a1c9e3b05" 647394 "Sisir_ExportGuidesPly`Sisir"
		)
		callbacks.removeScripts #cuiRegisterMenus id:#sisirMenu
		callbacks.addScript #cuiRegisterMenus sisirRegisterMenus id:#sisirMenu
	) else (
		-- 3ds Max 2022-2024: built once the UI is up.
		global sisirBuildLegacyMenu
		fn sisirBuildLegacyMenu = (
			local old = menuMan.findMenu "Sisir"
			if old != undefined do menuMan.unRegisterMenu old
			local menu = menuMan.createMenu "Sisir"
			menu.addItem (menuMan.createActionItem "Sisir_ImportScene" "Sisir") -1
			menu.addItem (menuMan.createSeparatorItem()) -1
			menu.addItem (menuMan.createActionItem "Sisir_ExportHairAbc" "Sisir") -1
			menu.addItem (menuMan.createActionItem "Sisir_ExportGuidesPly" "Sisir") -1
			local mainBar = menuMan.getMainMenuBar()
			-- Before Help, the last menu.
			mainBar.addItem (menuMan.createSubMenuItem "Sisir" menu) (mainBar.numItems())
			menuMan.updateMenuBar()
		)
		-- Taken out again at exit, so Max's saved menu file never keeps it.
		global sisirRemoveLegacyMenu
		fn sisirRemoveLegacyMenu = (
			local old = menuMan.findMenu "Sisir"
			if old != undefined do (menuMan.unRegisterMenu old; menuMan.updateMenuBar())
		)
		callbacks.removeScripts #postSystemStartup id:#sisirMenu
		callbacks.removeScripts #preSystemShutdown id:#sisirMenu
		callbacks.addScript #postSystemStartup "sisirBuildLegacyMenu()" id:#sisirMenu
		callbacks.addScript #preSystemShutdown "sisirRemoveLegacyMenu()" id:#sisirMenu
	)
)
