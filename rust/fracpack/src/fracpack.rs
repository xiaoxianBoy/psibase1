use custom_error::custom_error;
use std::mem;

custom_error! {pub Error
    EndOfStream     = "End of stream",
    OffsetTooSmall  = "Offset too small",
}
pub type Result<T> = std::result::Result<T, Error>;

fn read_u8_arr<const SIZE: usize>(src: &mut &[u8]) -> Result<[u8; SIZE]> {
    let mut bytes: [u8; SIZE] = [0; SIZE];
    bytes.copy_from_slice(src.get(..SIZE).ok_or(Error::EndOfStream)?);
    *src = &src[SIZE..];
    Ok(bytes)
}

pub trait Packable {
    const FIXED_SIZE: u32;
    fn pack_fixed(&self, dest: &mut Vec<u8>);
    fn repack_fixed(&self, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>);
    fn pack_variable(&self, dest: &mut Vec<u8>);
    fn pack(&self, dest: &mut Vec<u8>);
    fn unpack_inplace(&mut self, src: &mut &[u8]) -> Result<()>;
    fn unpack_inplace_skip_offset(&mut self, src: &mut &[u8]) -> Result<()>;

    fn unpack(src: &mut &[u8]) -> Result<Self>
    where
        Self: Default,
    {
        let mut result: Self = Default::default();
        result.unpack_inplace_skip_offset(src)?;
        Ok(result)
    }

    fn option_pack_fixed(opt: &Option<Self>, dest: &mut Vec<u8>)
    where
        Self: Sized,
    {
        self::option_pack_fixed(opt, dest)
    }

    fn option_repack_fixed(opt: &Option<Self>, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>)
    where
        Self: Sized,
    {
        self::option_repack_fixed(opt, fixed_pos, heap_pos, dest)
    }

    fn option_pack_variable(opt: &Option<Self>, dest: &mut Vec<u8>)
    where
        Self: Sized,
    {
        self::option_pack_variable(opt, dest)
    }

    fn option_unpack_inplace(opt: &mut Option<Self>, src: &mut &[u8]) -> Result<()>
    where
        Self: Sized + Default,
    {
        self::option_unpack_inplace(opt, src)
    }
} // Packable

fn option_pack_fixed<T: Packable>(_opt: &Option<T>, dest: &mut Vec<u8>) {
    dest.extend_from_slice(&1u32.to_le_bytes())
}

fn option_repack_fixed<T: Packable>(
    opt: &Option<T>,
    fixed_pos: u32,
    heap_pos: u32,
    dest: &mut Vec<u8>,
) {
    if opt.is_some() {
        dest[fixed_pos as usize..fixed_pos as usize + 4]
            .copy_from_slice(&(heap_pos - fixed_pos).to_le_bytes())
    }
}

fn option_pack_variable<T: Packable>(opt: &Option<T>, dest: &mut Vec<u8>) {
    if let Some(x) = opt {
        x.pack(dest)
    }
}

// TODO: check if past fixed area
fn option_unpack_inplace<T: Packable + Default>(
    opt: &mut Option<T>,
    outer: &mut &[u8],
) -> Result<()> {
    let orig: &[u8] = outer;
    let offset = u32::unpack(outer)?;
    if offset == 1 {
        *opt = None;
        return Ok(());
    }
    if offset < 4 {
        return Err(Error::OffsetTooSmall);
    }
    let mut inner = orig.get(offset as usize..).ok_or(Error::EndOfStream)?;
    *opt = Some(Default::default());
    if let Some(ref mut x) = *opt {
        T::unpack_inplace_skip_offset(x, &mut inner)?;
    }
    Ok(())
}

macro_rules! scalar_impl_fracpack {
    ($t:ty) => {
        impl Packable for $t {
            const FIXED_SIZE: u32 = mem::size_of::<$t>() as u32;
            fn pack_fixed(&self, dest: &mut Vec<u8>) {
                dest.extend_from_slice(&self.to_le_bytes());
            }
            fn repack_fixed(&self, _fixed_pos: u32, _heap_pos: u32, _dest: &mut Vec<u8>) {}
            fn pack_variable(&self, _dest: &mut Vec<u8>) {}
            fn pack(&self, dest: &mut Vec<u8>) {
                self.pack_fixed(dest)
            }
            fn unpack_inplace(&mut self, src: &mut &[u8]) -> Result<()> {
                self.unpack_inplace_skip_offset(src)
            }
            fn unpack_inplace_skip_offset(&mut self, src: &mut &[u8]) -> Result<()> {
                *self = <$t>::from_le_bytes(read_u8_arr(src)?.into());
                Ok(())
            }
        }
    };
}

scalar_impl_fracpack! {i8}
scalar_impl_fracpack! {i16}
scalar_impl_fracpack! {i32}
scalar_impl_fracpack! {i64}
scalar_impl_fracpack! {u8}
scalar_impl_fracpack! {u16}
scalar_impl_fracpack! {u32}
scalar_impl_fracpack! {u64}
scalar_impl_fracpack! {f32}
scalar_impl_fracpack! {f64}

impl<T: Packable + Sized + Default> Packable for Option<T> {
    const FIXED_SIZE: u32 = 4;

    fn pack_fixed(&self, dest: &mut Vec<u8>) {
        T::option_pack_fixed(self, dest);
    }

    fn repack_fixed(&self, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>) {
        T::option_repack_fixed(self, fixed_pos, heap_pos, dest)
    }

    fn pack_variable(&self, dest: &mut Vec<u8>) {
        T::option_pack_variable(self, dest)
    }

    fn pack(&self, _dest: &mut Vec<u8>) {
        todo!("Can option<T> be at the top level?")
    }

    fn unpack_inplace(&mut self, src: &mut &[u8]) -> Result<()> {
        T::option_unpack_inplace(self, src)
    }

    fn unpack_inplace_skip_offset(&mut self, _src: &mut &[u8]) -> Result<()> {
        todo!("Does the spec support Option<Option<T>> or top-level Option<T>?")
    }

    fn unpack(_src: &mut &[u8]) -> Result<Self> {
        todo!("Can option<T> be at the top level?")
    }

    fn option_pack_fixed(_opt: &Option<Self>, _dest: &mut Vec<u8>) {
        todo!("Does the spec support Option<Option<T>>?")
    }

    fn option_repack_fixed(
        _opt: &Option<Self>,
        _fixed_pos: u32,
        _heap_pos: u32,
        _dest: &mut Vec<u8>,
    ) {
        todo!("Does the spec support Option<Option<T>>?")
    }

    fn option_pack_variable(_opt: &Option<Self>, _dest: &mut Vec<u8>) {
        todo!("Does the spec support Option<Option<T>>?")
    }

    fn option_unpack_inplace(_opt: &mut Option<Self>, _src: &mut &[u8]) -> Result<()> {
        todo!("Does the spec support Option<Option<T>>?")
    }
} // impl Packable for Option<T>

impl Packable for String {
    const FIXED_SIZE: u32 = 4;

    fn pack_fixed(&self, dest: &mut Vec<u8>) {
        dest.extend_from_slice(&0u32.to_le_bytes());
    }

    fn repack_fixed(&self, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>) {
        if self.is_empty() {
            return;
        }
        dest[fixed_pos as usize..fixed_pos as usize + 4]
            .copy_from_slice(&(heap_pos - fixed_pos).to_le_bytes());
    }

    fn pack_variable(&self, dest: &mut Vec<u8>) {
        if self.is_empty() {
            return;
        }
        dest.extend_from_slice(&(self.len() as u32).to_le_bytes());
        dest.extend_from_slice(self.as_bytes());
    }

    fn pack(&self, _dest: &mut Vec<u8>) {
        todo!("Does the spec support top-level string?");
    }

    fn unpack_inplace(&mut self, outer: &mut &[u8]) -> Result<()> {
        let orig: &[u8] = outer;
        let offset = u32::unpack(outer)?;
        if offset == 0 {
            self.clear();
            return Ok(());
        }
        if offset < 4 {
            return Err(Error::OffsetTooSmall);
        }
        let mut inner = orig.get(offset as usize..).ok_or(Error::EndOfStream)?;
        self.unpack_inplace_skip_offset(&mut inner)
    }

    fn unpack_inplace_skip_offset(&mut self, src: &mut &[u8]) -> Result<()> {
        let len = u32::unpack(src)?;
        let bytes = src.get(..len as usize).ok_or(Error::EndOfStream)?;
        *src = src.get(len as usize..).ok_or(Error::EndOfStream)?;
        // TODO: from_utf8 (error if bad) instead?
        *self = String::from_utf8_lossy(bytes).into_owned();
        Ok(())
    }

    fn unpack(_src: &mut &[u8]) -> Result<Self> {
        todo!("Does the spec support top-level string?");
    }

    fn option_pack_fixed(_opt: &Option<Self>, dest: &mut Vec<u8>) {
        dest.extend_from_slice(&1u32.to_le_bytes())
    }

    fn option_repack_fixed(opt: &Option<Self>, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>) {
        if let Some(x) = opt {
            if x.is_empty() {
                dest[fixed_pos as usize..fixed_pos as usize + 4]
                    .copy_from_slice(&(0 as u32).to_le_bytes())
            } else {
                dest[fixed_pos as usize..fixed_pos as usize + 4]
                    .copy_from_slice(&(heap_pos - fixed_pos).to_le_bytes())
            }
        }
    }

    fn option_pack_variable(opt: &Option<Self>, dest: &mut Vec<u8>) {
        if let Some(x) = opt {
            x.pack_variable(dest)
        }
    }

    fn option_unpack_inplace(opt: &mut Option<Self>, outer: &mut &[u8]) -> Result<()> {
        let orig: &[u8] = outer;
        let offset = u32::unpack(outer)?;
        if offset == 1 {
            *opt = None;
            return Ok(());
        }
        if offset == 0 {
            *opt = Some(String::from(""));
            return Ok(());
        }
        if offset < 4 {
            return Err(Error::OffsetTooSmall);
        }
        let mut inner = orig.get(offset as usize..).ok_or(Error::EndOfStream)?;
        *opt = Some(Default::default());
        if let Some(ref mut x) = *opt {
            String::unpack_inplace_skip_offset(x, &mut inner)?;
        }
        Ok(())
    }
} // impl Packable for String

impl<T: Packable + Default + Clone> Packable for Vec<T> {
    const FIXED_SIZE: u32 = 4;

    fn pack_fixed(&self, dest: &mut Vec<u8>) {
        dest.extend_from_slice(&0u32.to_le_bytes());
    }

    fn repack_fixed(&self, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>) {
        if !self.is_empty() {
            dest[fixed_pos as usize..fixed_pos as usize + 4]
                .copy_from_slice(&(heap_pos - fixed_pos).to_le_bytes());
        }
    }

    fn pack_variable(&self, dest: &mut Vec<u8>) {
        if self.is_empty() {
            return;
        }
        dest.extend_from_slice(&(self.len() as u32).to_le_bytes());
        dest.reserve(self.len() * (T::FIXED_SIZE as usize));
        let start = dest.len();
        for x in self {
            x.pack_fixed(dest);
        }
        for (i, x) in self.iter().enumerate() {
            let heap_pos = dest.len() as u32;
            x.repack_fixed(start as u32 + (i as u32) * T::FIXED_SIZE, heap_pos, dest);
            x.pack_variable(dest);
        }
    }

    fn pack(&self, _dest: &mut Vec<u8>) {
        todo!("Does the spec support top-level vector?");
    }

    fn unpack_inplace(&mut self, outer: &mut &[u8]) -> Result<()> {
        let orig: &[u8] = outer;
        let offset = u32::unpack(outer)?;
        if offset == 0 {
            self.clear();
            return Ok(());
        }
        if offset < 4 {
            return Err(Error::OffsetTooSmall);
        }
        let mut inner = orig.get(offset as usize..).ok_or(Error::EndOfStream)?;
        self.unpack_inplace_skip_offset(&mut inner)
    }

    fn unpack_inplace_skip_offset(&mut self, src: &mut &[u8]) -> Result<()> {
        let len = u32::unpack(src)?;
        self.clear();
        self.resize(len as usize, Default::default());
        for x in self {
            x.unpack_inplace(src)?;
        }
        Ok(())
    }

    fn option_pack_fixed(_opt: &Option<Self>, dest: &mut Vec<u8>)
    where
        Self: Sized,
    {
        dest.extend_from_slice(&1u32.to_le_bytes())
    }

    fn option_repack_fixed(opt: &Option<Self>, fixed_pos: u32, heap_pos: u32, dest: &mut Vec<u8>)
    where
        Self: Sized,
    {
        if let Some(x) = opt {
            if x.is_empty() {
                dest[fixed_pos as usize..fixed_pos as usize + 4]
                    .copy_from_slice(&(0 as u32).to_le_bytes())
            } else {
                dest[fixed_pos as usize..fixed_pos as usize + 4]
                    .copy_from_slice(&(heap_pos - fixed_pos).to_le_bytes())
            }
        }
    }

    fn option_pack_variable(opt: &Option<Self>, dest: &mut Vec<u8>)
    where
        Self: Sized,
    {
        if let Some(x) = opt {
            x.pack_variable(dest)
        }
    }

    fn option_unpack_inplace(opt: &mut Option<Self>, outer: &mut &[u8]) -> Result<()>
    where
        Self: Sized,
    {
        let orig: &[u8] = outer;
        let offset = u32::unpack(outer)?;
        if offset == 1 {
            *opt = None;
            return Ok(());
        }
        if offset == 0 {
            *opt = Some(Default::default());
            return Ok(());
        }
        if offset < 4 {
            return Err(Error::OffsetTooSmall);
        }
        let mut inner = orig.get(offset as usize..).ok_or(Error::EndOfStream)?;
        *opt = Some(Default::default());
        if let Some(ref mut x) = *opt {
            Vec::<T>::unpack_inplace_skip_offset(x, &mut inner)?;
        }
        Ok(())
    }
} // impl<T> Packable for Vec<T>
