# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
[Global]
IconName=${MARUPOP_APPLICATION_ID}
Name=MaruPop
Comment=Japanese Popup Dictionary
DesktopEntry=${MARUPOP_APPLICATION_ID}

# Action is the *presentation* of an event, not the application's opt-in: KNotification drops
# any event whose Action is empty or None before it ever reaches the server, so every event
# here has to say Popup. The opt-in is one setting per channel: the three events below that
# announce finished work are raised only where the user asked for the work, and failed follows
# NotifyOnError (default true). Leaving Sound out of Action keeps the profile silent. These are
# only defaults — System Settings still overrides them per event.

[Event/dictionaryImported]
Name=Dictionary Imported
Comment=A dictionary was imported
Action=Popup

[Event/dictionaryUpdateAvailable]
Name=Dictionary Update Available
Comment=A dictionary update is available
Action=Popup

[Event/modelsDownloaded]
Name=Recognition Models Downloaded
Comment=Text recognition models were downloaded and verified
Action=Popup

[Event/failed]
Name=Operation Failed
Comment=A capture, recognition, download, or import failed
Action=Popup
