"""Original synthetic fixtures: no game resources required."""
import importlib.util
import json
import os
import pathlib
import struct
import tempfile
import unittest

import actor_model as model
import test_notebook_model as fixture

ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('carved_post',ROOT/'examples/guest-mods/sdk-v3-windfall/assets/make_carved_post.py')
asset=importlib.util.module_from_spec(spec); spec.loader.exec_module(asset)


def static_template():
    data = bytearray(fixture.template())
    # Original synthetic one-root skeleton with a tilted/translating rest pose.
    fm, skeleton, bone = 0x200, 0x700, 0x740
    struct.pack_into('>i', data, fm+0xC, skeleton-(fm+0xC))
    data[skeleton:skeleton+4] = b'FSKL'
    struct.pack_into('>HHH', data, skeleton+8, 1, 0, 0)
    struct.pack_into('>i', data, skeleton+0x14, bone-(skeleton+0x14))
    struct.pack_into('>H', data, bone+6, 0xFFFF)
    struct.pack_into('>I', data, bone+0x10, 0x03001001)
    struct.pack_into('>3f', data, bone+0x14, 2, 3, 4)
    struct.pack_into('>4f', data, bone+0x20, .2, .3, .4, 1)
    struct.pack_into('>3f', data, bone+0x30, 10, 20, 30)
    return bytes(data)


def mutate(data, change):
    length=struct.unpack_from('<I',data,12)[0]; doc=json.loads(data[20:20+length]); change(doc)
    text=json.dumps(doc).encode(); text+=b' '*(-len(text)%4)
    body=struct.pack('<I4s',len(text),b'JSON')+text+data[20+length:]
    return b'glTF'+struct.pack('<II',2,12+len(body))+body


class ActorModelTests(unittest.TestCase):
    def parse(self,data):
        with tempfile.TemporaryDirectory() as directory:
            path=os.path.join(directory,'model.glb'); pathlib.Path(path).write_bytes(data)
            return model.read_glb(path)

    def test_original_asset_reproducible(self):
        data=asset.make()
        self.assertEqual(data,(ROOT/'examples/guest-mods/sdk-v3-windfall/assets/carved_post.glb').read_bytes())
        geometry,info=self.parse(data)
        self.assertEqual(info,dict(vertices=24,triangles=12,texture_width=128,texture_height=128))
        self.assertAlmostEqual(max(p[1] for p in geometry[0]),160,places=4)
        self.assertEqual(min(p[1] for p in geometry[0]),0)

    def test_transform_and_mirrored_winding(self):
        original,_=self.parse(asset.make())
        data=mutate(asset.make(),lambda d:d['nodes'][0].update(translation=[1,2,3],scale=[-2,1,1]))
        transformed,_=self.parse(data)
        self.assertAlmostEqual(transformed[0][0][0],160,places=4)
        self.assertAlmostEqual(transformed[0][0][1],200,places=4)
        self.assertEqual(transformed[3][:3],[original[3][0],original[3][2],original[3][1]])

    def test_golden_bfres_structure_identity_and_origin(self):
        geometry,_=self.parse(asset.make())
        # Synthetic fixture carries strings but no original resources.
        output=model.build_bfres(static_template(),geometry,'mod_123abc')
        f=model.base.Bfres(output); dictionary=f.rel(0x20); name,fm=f.dict(dictionary)[0]
        self.assertEqual(name,'mod_123abc'); self.assertEqual(f.cstr(f.rel(fm+4)),name)
        self.assertEqual(f.u32(dictionary+24),(len(name)-1)*8+(ord(name[-1]).bit_length()-1))
        self.assertEqual(struct.unpack_from('>IHH',output,dictionary+8),(0xffffffff,1,0))
        vertices=f.rel(f.rel(f.rel(fm+0x10)+0x18)+0x14)
        for actual, expected in zip(struct.unpack_from('>3f',output,vertices),geometry[0][0]): self.assertAlmostEqual(actual,expected,places=4)
        shape=f.dict(f.rel(fm+0x14))[0][1]; box=f.rel(shape+0x34)
        self.assertAlmostEqual(struct.unpack_from('>3f',output,box)[1],80,places=4)
        mesh=f.rel(shape+0x24); self.assertEqual(f.u32(mesh+8),36)
        self.assertEqual(f.u32(0xC),len(output))
        skeleton=f.rel(fm+0xC); bone=f.rel(skeleton+0x14)
        self.assertEqual(struct.unpack_from('>3f',output,bone+0x14),(1,1,1))
        self.assertEqual(struct.unpack_from('>4f',output,bone+0x20),(0,0,0,1))
        self.assertEqual(struct.unpack_from('>3f',output,bone+0x30),(0,0,0))
        self.assertEqual(f.u32(bone+0x10),0x03001001)

    def test_reject_invalid_contract(self):
        changes=[lambda d:d.update(skins=[{}]),lambda d:d.update(animations=[{}]),lambda d:d['nodes'][0].update(scale=[0,1,1]),lambda d:d['nodes'][0].update(translation=[float('nan'),0,0]),lambda d:d['accessors'][0].update(count=4097),lambda d:d['accessors'][3].update(count=24579),lambda d:d['accessors'][0].update(byteOffset=999999),lambda d:d['meshes'][0]['primitives'][0].update(mode=1),lambda d:d['buffers'][0].update(uri='external.bin'),lambda d:d['nodes'][0].update(children=[0]),lambda d:d['accessors'][0].update(sparse={}),lambda d:d['materials'][0].update(alphaMode='BLEND')]
        for change in changes:
            with self.subTest(change=change),self.assertRaises(model.BuildError): self.parse(mutate(asset.make(),change))

    def test_output_validator_rejects_corruption(self):
        geometry,_=self.parse(asset.make())
        output=bytearray(model.build_bfres(static_template(),geometry,'unique'))
        struct.pack_into('>I',output,0xC,1)
        with self.assertRaises(model.BuildError): model.validate_bfres(output,'unique',24,36)

    def test_nonuniform_normal_transform_and_hierarchy(self):
        def change(doc):
            doc['nodes']=[{'translation':[1,0,0],'children':[1]},{'mesh':0,'scale':[2,3,4]}]
        geometry,_=self.parse(mutate(asset.make(),change))
        self.assertAlmostEqual(geometry[0][0][0],40,places=4)
        for normal in geometry[1]: self.assertAlmostEqual(sum(v*v for v in normal),1)

    def test_png_bounds_and_crc(self):
        for w,h in ((513,8),(64,513),(63,8),(64,7)):
            with self.assertRaises(model.BuildError): model.png(fixture.png(w,h,bytes(w*h*4)))
        image=bytearray(fixture.png(64,8,bytes(64*8*4))); image[-1]^=1
        with self.assertRaises(model.BuildError): model.png(image)

    def test_truncated_and_oversized(self):
        for data in (b'',asset.make()[:-1],asset.make()[:20]):
            with self.assertRaises(model.BuildError): self.parse(data)
        with self.assertRaises(model.BuildError): self.parse(b'x'*(model.MAX_SOURCE+1))

    def test_identity(self):
        geometry,_=self.parse(asset.make())
        for name in ('../bad','UPPER','a'*32,''):
            with self.assertRaises(model.BuildError): model.build_bfres(static_template(),geometry,name)

    def test_archive_and_cache_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            game=pathlib.Path(directory); archive=game/model.base.ARCHIVE; archive.parent.mkdir(parents=True)
            shader=b'original synthetic shader marker'
            archive.write_bytes(model.base.yaz0_stored(model.base.sarc({'Vbeso.bfres':static_template(),'vbeso.sharcfb':shader})))
            glb=game/'source.glb'; glb.write_bytes(asset.make()); out=str(game/'cache'/'unique.szs')
            one=model.build(str(game),str(glb),'unique',out)
            files=dict(model.base.shaderprep.sarc(model.base.shaderprep.yaz0(pathlib.Path(out).read_bytes())))
            self.assertEqual(set(files),{'unique.bfres','unique.sharcfb'}); self.assertEqual(files['unique.sharcfb'],shader)
            self.assertEqual(json.loads(pathlib.Path(out+'.json').read_text()),one)
            before = pathlib.Path(out).stat().st_mtime_ns
            from unittest.mock import patch
            with patch.object(model,'build_bfres',side_effect=AssertionError('valid cache was rebuilt')):
                two=model.build(str(game),str(glb),'unique',out)
            self.assertEqual(one,two); self.assertEqual(before,pathlib.Path(out).stat().st_mtime_ns)
            pathlib.Path(out).write_bytes(b'corrupt')
            rebuilt=model.build(str(game),str(glb),'unique',out); self.assertEqual(rebuilt,one)
            pathlib.Path(out).unlink()
            self.assertEqual(model.build(str(game),str(glb),'unique',out),one)
            pathlib.Path(out+'.json').write_text('{invalid')
            self.assertEqual(model.build(str(game),str(glb),'unique',out),one)
            # Player-local template changes invalidate the key even when source
            # geometry and identity stay unchanged.
            archive.write_bytes(model.base.yaz0_stored(model.base.sarc({'Vbeso.bfres':static_template(),'vbeso.sharcfb':shader+b'new'})))
            changed=model.build(str(game),str(glb),'unique',out)
            self.assertNotEqual(one['cache_key'],changed['cache_key'])
            glb.write_bytes(mutate(asset.make(),lambda doc:doc['nodes'][0].update(translation=[1,0,0])))
            changed_source=model.build(str(game),str(glb),'unique',out)
            self.assertNotEqual(changed['cache_key'],changed_source['cache_key'])
            other=model.build(str(game),str(glb),'other',out); self.assertNotEqual(one['cache_key'],other['cache_key'])


if __name__=='__main__': unittest.main()
