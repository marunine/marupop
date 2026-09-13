<?xml version="1.0" encoding="UTF-8"?>
<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: CC0-1.0 -->
<component type="desktop-application">
  <id>${MARUPOP_APPLICATION_ID}</id>
  <launchable type="desktop-id">${MARUPOP_APPLICATION_ID}.desktop</launchable>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>LGPL-3.0-only</project_license>

  <developer id="io.github.marunine">
    <name translate="no">marunine</name>
  </developer>

  <name>MaruPop</name>
  <summary>Look up Japanese words under the mouse pointer</summary>

  <description>
    <p>
      MaruPop recognizes Japanese text on screen and displays dictionary entries in a popup.
      Supported sources include games, videos, PDFs, and images.
    </p>
    <p>
      Control scanning from the system tray. Global shortcuts toggle scanning, copy words, and
      pin the popup. A pinned popup supports scrolling and clicking.
    </p>
    <p>Recognition:</p>
    <ul>
      <li>Horizontal and vertical Japanese text</li>
      <li>Offline text recognition with meikiocr models</li>
      <li>Support for separately installed Chrome Screen AI</li>
    </ul>
    <p>Lookup:</p>
    <ul>
      <li>Verb and adjective deconjugation</li>
      <li>Downloads and updates for JMdict, JMnedict, and KANJIDIC2</li>
      <li>Yomitan word, kanji, frequency, and pitch accent dictionaries</li>
      <li>Custom word and name lists</li>
    </ul>
    <p>Popup:</p>
    <ul>
      <li>Automatic positioning near the mouse pointer</li>
      <li>Configurable colors, fonts, and content</li>
      <li>Pitch accent graphs above readings</li>
    </ul>
  </description>

  <content_rating type="oars-1.1"/>

  <url type="homepage">https://github.com/marunine/marupop</url>
  <url type="bugtracker">https://github.com/marunine/marupop/issues</url>
  <url type="vcs-browser">https://github.com/marunine/marupop</url>

  <provides>
    <binary>marupop</binary>
  </provides>

  <!-- Add a dated release entry when a release is published. -->
  <releases/>
</component>
