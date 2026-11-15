import QtQuick

FocusScope {
    id: moduleRoot

    signal goBack()

    property var navParams: ({})

    property string moduleId: "com.240mp.pc_stats"
    property var _moduleInfo: appCore ? appCore.get_module_info(moduleId) : ({})
    property string moduleName: _moduleInfo.name || ""
    property string moduleIcon: _moduleInfo.icon || ""

    property var navStack: []
    property var currentParams: ({})

    function navigateTo(viewPath, params, fromState) {
        var resolved = Qt.resolvedUrl(viewPath)
        navStack.push({ source: internalLoader.source, params: currentParams, listState: fromState || {} })
        currentParams = params || {}
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function replaceWith(viewPath, params) {
        var resolved = Qt.resolvedUrl(viewPath)
        currentParams = params || {}
        navStack = []
        internalLoader.setSource(resolved, { "navParams": params || {} })
    }

    function navigateBack() {
        if (navStack.length === 0) {
            moduleRoot.goBack()
            return
        }
        var prev = navStack.pop()
        if (!prev.source || prev.source.toString() === "") {
            moduleRoot.goBack()
            return
        }
        currentParams = prev.params || {}
        internalLoader.setSource(prev.source, { "navParams": currentParams })
    }

    Loader {
        id: internalLoader
        anchors.fill: parent
        focus: true
        onLoaded: { if (item) item.forceActiveFocus() }

        Connections {
            target: internalLoader.item
            ignoreUnknownSignals: true
            function onNavigateTo(path, params, listState) { moduleRoot.navigateTo(path, params, listState) }
            function onReplaceWith(path, params) { moduleRoot.replaceWith(path, params) }
            function onGoBack() { moduleRoot.navigateBack() }
        }
    }

    // An empty host is the only reason to skip the live view. The default
    // address is filled in by the backend, so this is the "I cleared it" path.
    Component.onCompleted: {
        if (pcStatsBackend && pcStatsBackend.configured)
            replaceWith("Stats.qml", {})
        else
            replaceWith("Setup.qml", {})
    }

    // Stop polling as the module goes away, including when playback or another
    // module replaces this screen. Nothing here marks the user active.
    Component.onDestruction: {
        if (pcStatsBackend)
            pcStatsBackend.stop()
    }
}
