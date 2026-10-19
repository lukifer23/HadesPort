use pdb::FallibleIterator;
use serde_json::json;
fn main() -> Result<(), Box<dyn std::error::Error>> {
 let path=std::env::args().nth(1).ok_or("PDB path required")?;
 let mut pdb=pdb::PDB::open(std::fs::File::open(&path)?)?;
 let info=pdb.pdb_information()?;
 let identity=json!({"path":path,"guid":format!("{}",info.guid),"age":info.age,"signature":info.signature});
 let map=pdb.address_map()?;
 let globals=pdb.global_symbols()?;
 let mut it=globals.iter(); let mut records=vec![];
 while let Some(sym)=it.next()? {
  if let Ok(data)=sym.parse() {
   match data {
    pdb::SymbolData::Public(v)=>records.push(json!({"kind":"public","name":v.name.to_string(),"rva":v.offset.to_rva(&map).map(|x|x.0),"function":v.function})),
    pdb::SymbolData::Data(v)=>records.push(json!({"kind":"data","name":v.name.to_string(),"rva":v.offset.to_rva(&map).map(|x|x.0),"type_index":v.type_index.0})),
    _=>{}
   }
  }
 }
 let dbi=pdb.debug_information()?;let mut modules=dbi.modules()?;let mut module_records=vec![];
 while let Some(module)=modules.next()? {
  let name=module.module_name().to_string();
  if let Some(mi)=pdb.module_info(&module)? {
   let mut it=mi.symbols()?;
   while let Some(sym)=it.next()? {
    if let Ok(data)=sym.parse() {
     match data {
      pdb::SymbolData::Procedure(v)=>module_records.push(json!({"kind":"procedure","module":name,"name":v.name.to_string(),"rva":v.offset.to_rva(&map).map(|x|x.0),"length":v.len,"type_index":v.type_index.0})),
      pdb::SymbolData::Data(v)=>module_records.push(json!({"kind":"data","module":name,"name":v.name.to_string(),"rva":v.offset.to_rva(&map).map(|x|x.0),"type_index":v.type_index.0})),
      _=>{}
     }
    }
   }
  }
 }
 let types=pdb.type_information()?;let mut type_records=vec![];let mut wanted=std::collections::HashSet::new();
 let mut ti=types.iter();
 while let Some(t)=ti.next()? {
  if let Ok(d)=t.parse() {
   if [0xd517,0xdbd4,0x1065,0xd716,0xd715].contains(&t.index().0) {type_records.push(json!({"index":t.index().0,"debug":format!("{:?}",d)}));}
   match &d {
    pdb::TypeData::Enumeration(v) if v.name.to_string().contains("ScriptEnums") || v.name.to_string().contains("Trigger") || v.name.to_string().contains("FileType") || v.name.to_string().contains("FilePath") || v.name.to_string().contains("BlendMode") || v.name.to_string().contains("WeaponType") || v.name.to_string().contains("EffectType") || v.name.to_string().contains("ProjectileOffset") || v.name.to_string().contains("ProjectileType") || v.name.to_string()=="sgg::ProjectileDataDef::Allegiance" || v.name.to_string().contains("CollisionCheck") || v.name.to_string().contains("SortMode") || v.name.to_string().contains("MeshType") || v.name.to_string().contains("LifeStatus") => {
     wanted.insert(v.fields.0);type_records.push(json!({"index":t.index().0,"name":v.name.to_string(),"debug":format!("{:?}",d)}));
    },
    pdb::TypeData::Union(v) if v.name.to_string().contains("Color") => {
     wanted.insert(v.fields.0);type_records.push(json!({"index":t.index().0,"name":v.name.to_string(),"debug":format!("{:?}",d)}));
    },
    pdb::TypeData::Class(v) if v.name.to_string().contains("TransitionHelper") || ["sgg::Camera","sgg::ProjectileData","sgg::ProjectileDataDef","sgg::Entity","sgg::LifeComponent","sgg::LifeData","sgg::LifeDataDef","sgg::ObstacleData","sgg::ObstacleDataDef","sgg::Physics","sgg::UnitStatus","sgg::MoveAI","sgg::MoveAIData","sgg::MoveAIDataDef","sgg::SoundCue","sgg::EffectData","sgg::EffectDataDef","sgg::Weapon","sgg::WeaponData","sgg::WeaponDataDef","sgg::WeaponArsenal","sgg::RemappableControl","sgg::Player","sgg::PlayerUnitInput","sgg::Obstacle","sgg::BodyComponent","sgg::Body","sgg::Animation","sgg::ThingPhysics","sgg::Physics","sgg::GUIComponentTextBox","sgg::TextComponent","sgg::TextSystem","sgg::ComponentData","sgg::ComponentDataDef","sgg::TextBoxData","sgg::ScriptArgs","Lua","LuaFunction","LuaTable","sgg::Unit","sgg::UnitData","sgg::PhysicsComponent","sgg::PlayerUnitInput","sgg::PlayerUnitData","sgg::InputHandler","sgg::Thing","sgg::World","sgg::Random","sgg::Script"].contains(&v.name.to_string().as_ref()) || v.name.to_string().contains("UnitDataDef") || v.name.to_string().contains("MapThingDef") || v.name.to_string().contains("AtlasMap") || v.name.to_string()=="sgg::Drawable" || ["sgg::AnimationDataDef","sgg::AnimationData","sgg::ThingDataDef","sgg::ThingData","sgg::MapThing","sgg::Color","sgg::TColor","sgg::TColor<float>","sgg::TColor<unsigned char>"].contains(&v.name.to_string().as_ref()) => {
     if let Some(f)=v.fields {wanted.insert(f.0);}
     type_records.push(json!({"index":t.index().0,"name":v.name.to_string(),"debug":format!("{:?}",d)}));
    },
    _=>{}
   }
  }
 }
 let mut ti=types.iter();
 while let Some(t)=ti.next()? {
  if wanted.contains(&t.index().0) {type_records.push(json!({"index":t.index().0,"debug":format!("{:?}",t.parse()?)}));}
 }
 println!("{}",serde_json::to_string_pretty(&json!({"identity":identity,"globals":records,"module_symbols":module_records,"selected_types":type_records}))?);
 Ok(())
}
