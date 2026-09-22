#!/usr/bin/env python3
"""Exercise the real exporter: python3 scripts/test_xcode_standalone_scheme.py /path/to/Projucer"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='projucerg-schemes-') as temp:
    root = Path(temp)
    project = root / 'Test.jucer'
    model = ET.fromstring('''<JUCERPROJECT id="ScheMe" name="Scheme &amp; Test" projectType="audioplug"
        pluginFormats="buildStandalone,buildVST3" pluginManufacturer="Test"
        pluginManufacturerCode="Test" pluginCode="Schm">
        <MAINGROUP id="main" name="Test"/><MODULES/><EXPORTFORMATS/>
        </JUCERPROJECT>''')
    exporters = []
    for tag, platform in [('XCODE_MAC', 'MacOSX'), ('XCODE_IPHONE', 'iOS')]:
        exporter = ET.SubElement(model.find('EXPORTFORMATS'), tag, targetFolder=f'Builds/{platform}')
        configs = ET.SubElement(exporter, 'CONFIGURATIONS')
        ET.SubElement(configs, 'CONFIGURATION', name='Dev Custom', isDebug='1', targetName='Debug & App')
        ET.SubElement(configs, 'CONFIGURATION', name='Ship Custom', isDebug='0', targetName='Release App')
        exporters.append(exporter)

    def save():
        ET.ElementTree(model).write(project, encoding='utf-8', xml_declaration=True)
        subprocess.run([str(binary), '--resave', str(project)], check=True, capture_output=True)

    def bundles():
        return [root / 'Builds' / p / (model.get('name') + '.xcodeproj') for p in ['MacOSX', 'iOS']]

    def schemes():
        return [b / 'xcshareddata/xcschemes' / (model.get('name') + ' - Standalone Plugin.xcscheme') for b in bundles()]

    save()
    for bundle, file in zip(bundles(), schemes()):
        xml = ET.parse(file).getroot()
        assert xml.find('LaunchAction').get('buildConfiguration') == 'Dev Custom'
        assert xml.find('ProfileAction').get('buildConfiguration') == 'Ship Custom'
        refs = xml.findall('.//BuildableReference')
        assert refs[1].get('BuildableName') == 'Debug & App.app'
        assert refs[2].get('BuildableName') == 'Release App.app'
        pbx = subprocess.run(['plutil', '-convert', 'json', '-o', '-', str(bundle / 'project.pbxproj')],
                             check=True, capture_output=True, text=True)
        objects = json.loads(pbx.stdout)['objects']
        for ref in refs:
            target = objects[ref.get('BlueprintIdentifier')]
            assert target['isa'] == 'PBXNativeTarget'
            assert target['name'] == ref.get('BlueprintName')
        listed = subprocess.run(['xcodebuild', '-list', '-json', '-project', str(bundle)],
                                check=True, capture_output=True, text=True)
        assert model.get('name') + ' - Standalone Plugin' in json.loads(listed.stdout)['project']['schemes']
        settings = subprocess.run(['xcodebuild', '-showBuildSettings', '-json', '-project', str(bundle),
                                   '-scheme', model.get('name') + ' - Standalone Plugin'],
                                  check=True, capture_output=True, text=True)
        standalone = next(s for s in json.loads(settings.stdout) if s['target'].endswith('Standalone Plugin'))
        assert standalone['buildSettings']['CONFIGURATION'] == 'Dev Custom'
        assert standalone['buildSettings']['FULL_PRODUCT_NAME'] == 'Debug & App.app'

    before = [(s.read_bytes(), s.stat().st_mtime_ns) for s in schemes()]
    sentinels = []
    for bundle in bundles():
        for relative in ['xcuserdata/check.xcuserdatad/xcschemes/xcschememanagement.plist',
                         'project.xcworkspace/custom-data', 'xcshareddata/xcschemes/Host.xcscheme']:
            path = bundle / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('preserve me')
            sentinels.append(path)
    save()
    assert before == [(s.read_bytes(), s.stat().st_mtime_ns) for s in schemes()]
    assert all(p.read_text() == 'preserve me' for p in sentinels)

    for exporter, file in zip(exporters, schemes()):
        exporter.set('keepCustomXcodeSchemes', '1')
        xml = ET.parse(file)
        ET.SubElement(xml.getroot().find('LaunchAction'), 'CommandLineArguments')
        xml.write(file)
    custom = [s.read_bytes() for s in schemes()]
    save()
    assert custom == [s.read_bytes() for s in schemes()]
    schemes()[0].unlink()
    save()
    assert schemes()[0].exists()  # Keep mode still supplies a missing scheme.
    assert schemes()[1].read_bytes() == custom[1]

    for exporter in exporters:
        exporter.set('keepCustomXcodeSchemes', '0')
    model.set('pluginFormats', 'buildVST3')
    save()
    assert all(not s.exists() for s in schemes())
    assert all(p.read_text() == 'preserve me' for p in sentinels)
    model.set('pluginFormats', 'buildStandalone,buildVST3')
    for exporter in exporters:
        configs = exporter.find('CONFIGURATIONS')
        configs.remove(configs[0])  # Release-only fallback.
    save()
    for file in schemes():
        assert ET.parse(file).find('LaunchAction').get('buildConfiguration') == 'Ship Custom'
    print('PASS: macOS/iOS discovery, target references, custom names, stable saves, preservation, removal/recreation, single configuration')
