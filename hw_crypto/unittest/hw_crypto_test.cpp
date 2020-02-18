// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>

extern "C" {
#include "multimac.h"
#include "oracle.h"
#include "noncegen.h"
#include "coinid.h"
#include "kdf.h"
#include "rangeproof.h"
#include "sign.h"
#include "keykeeper.h"
}

#include "../core/ecc_native.h"
#include "../core/block_crypt.h"

#include "../keykeeper/local_private_key_keeper.h"


using namespace beam;

int g_TestsFailed = 0;

const Height g_hFork = 3; // whatever

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*) p)[i] = (uint8_t) rand();
}

template <uint32_t nBytes>
void SetRandom(uintBig_t<nBytes>& x)
{
	GenerateRandom(x.m_pData, x.nBytes);
}

void SetRandom(ECC::Scalar::Native& x)
{
	ECC::Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

void SetRandom(ECC::Point::Native& value, uint8_t y = 0)
{
    ECC::Point p;

    SetRandom(p.m_X);
    p.m_Y = y;

    while (!value.Import(p))
    {
        verify_test(value == Zero);
        p.m_X.Inc();
    }
}

template <typename T>
void SetRandomOrd(T& x)
{
	GenerateRandom(&x, sizeof(x));
}

BeamCrypto_UintBig& Ecc2BC(const ECC::uintBig& x)
{
	static_assert(sizeof(x) == sizeof(BeamCrypto_UintBig));
	return (BeamCrypto_UintBig&) x;
}

BeamCrypto_Point& Ecc2BC(const ECC::Point& x)
{
	static_assert(sizeof(x) == sizeof(BeamCrypto_Point));
	return (BeamCrypto_Point&) x;
}

void BeamCrypto_InitGenSecure(BeamCrypto_MultiMac_Secure& x, const ECC::Point::Native& ptVal, const ECC::Point::Native& nums)
{
	ECC::Point::Compact::Converter cpc;
	ECC::Point::Native pt = nums;

	for (unsigned int i = 0; ; pt += ptVal)
	{
		assert(!(pt == Zero));
		cpc.set_Deferred(Cast::Up<ECC::Point::Compact>(x.m_pPt[i]), pt);
		if (++i == BeamCrypto_MultiMac_Secure_nCount)
			break;
	}

	pt = Zero;
	for (unsigned int iBit = BeamCrypto_nBits; iBit--; )
	{
		pt = pt * ECC::Two;

		if (!(iBit % BeamCrypto_MultiMac_Secure_nBits))
			pt += nums;
	}

	pt = -pt;
	cpc.set_Deferred(Cast::Up<ECC::Point::Compact>(x.m_pPt[BeamCrypto_MultiMac_Secure_nCount]), pt);
	cpc.Flush();
}

void BeamCrypto_InitFast(BeamCrypto_MultiMac_Fast& trg, const ECC::MultiMac::Prepared& p)
{
	const ECC::MultiMac::Prepared::Fast& src = p.m_Fast;

	static_assert(_countof(trg.m_pPt) <= _countof(src.m_pPt));

	for (uint32_t j = 0; j < _countof(trg.m_pPt); j++)
		trg.m_pPt[j] = src.m_pPt[j];
}

void InitContext()
{
	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();
	assert(pCtx);

	const ECC::Context& ctx = ECC::Context::get();

	ECC::Point::Native nums, pt;
	ctx.m_Ipp.m_GenDot_.m_Fast.m_pPt[0].Assign(nums, true); // whatever point, doesn't matter actually

	ctx.m_Ipp.G_.m_Fast.m_pPt[0].Assign(pt, true);
	BeamCrypto_InitGenSecure(pCtx->m_GenG, pt, nums);

	ctx.m_Ipp.J_.m_Fast.m_pPt[0].Assign(pt, true);
	BeamCrypto_InitGenSecure(pCtx->m_GenJ, pt, nums);

	static_assert(ECC::InnerProduct::nDim * 2 == BeamCrypto_MultiMac_Fast_Idx_H);

	for (uint32_t iGen = 0; iGen < ECC::InnerProduct::nDim * 2; iGen++)
		BeamCrypto_InitFast(pCtx->m_pGenFast[iGen], ECC::Context::get().m_Ipp.m_pGen_[0][iGen]);

	BeamCrypto_InitFast(pCtx->m_pGenFast[BeamCrypto_MultiMac_Fast_Idx_H], ECC::Context::get().m_Ipp.H_);
}


void TestMultiMac()
{
	ECC::Mode::Scope scope(ECC::Mode::Fast);

	uint32_t aa = sizeof(BeamCrypto_MultiMac_Secure);
	uint32_t bb = sizeof(BeamCrypto_MultiMac_Fast);
	uint32_t cc = sizeof(BeamCrypto_MultiMac_WNaf);
	aa;  bb; cc;

	const uint32_t nFast = 8;
	const uint32_t nSecure = 2;

	const uint32_t nBatch = nFast + nSecure;

	BeamCrypto_MultiMac_WNaf pWnaf[nFast];
	BeamCrypto_MultiMac_Scalar pFastS[nFast];

	BeamCrypto_MultiMac_Secure pGenSecure[nSecure];
	secp256k1_scalar pSecureS[nSecure];

	BeamCrypto_MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = nullptr;
	mmCtx.m_Fast = nFast;
	mmCtx.m_Secure = nSecure;
	mmCtx.m_pGenFast = BeamCrypto_Context_get()->m_pGenFast;
	mmCtx.m_pS = pFastS;
	mmCtx.m_pWnaf = pWnaf;
	mmCtx.m_pGenSecure = pGenSecure;
	mmCtx.m_pSecureK = pSecureS;

	ECC::MultiMac_WithBufs<1, nBatch> mm1;

	for (uint32_t iGen = 0; iGen < nFast; iGen++)
	{
		const ECC::MultiMac::Prepared& p = ECC::Context::get().m_Ipp.m_pGen_[0][iGen];
		mm1.m_ppPrepared[iGen] = &p;
	}

	ECC::Point::Native ptVal, nums;
	ECC::Context::get().m_Ipp.m_GenDot_.m_Fast.m_pPt[0].Assign(nums, true); // whatever point, doesn't matter actually

	for (uint32_t iGen = 0; iGen < nSecure; iGen++)
	{
		const ECC::MultiMac::Prepared& p = ECC::Context::get().m_Ipp.m_pGen_[0][nFast + iGen];
		mm1.m_ppPrepared[nFast + iGen] = &p;

		p.m_Fast.m_pPt[0].Assign(ptVal, true);

		BeamCrypto_InitGenSecure(pGenSecure[iGen], ptVal, nums);
	}


	for (int i = 0; i < 10; i++)
	{
		mm1.Reset();

		for (uint32_t iPt = 0; iPt < nBatch; iPt++)
		{
			ECC::Scalar::Native sk;
			SetRandom(sk);

			mm1.m_pKPrep[iPt] = sk;
			mm1.m_Prepared++;

			if (iPt < nFast)
				pFastS[iPt].m_pK[0] = sk.get();
			else
				pSecureS[iPt - nFast] = sk.get();
		}

		ECC::Point::Native res1, res2;
		mm1.Calculate(res1);

		mmCtx.m_pRes = &res2.get_Raw();
		BeamCrypto_MultiMac_Calculate(&mmCtx);

		verify_test(res1 == res2);
	}
}

void TestNonceGen()
{
	static const char szSalt[] = "my_salt";

	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value seed;
		SetRandom(seed);

		ECC::NonceGenerator ng1(szSalt);
		ng1 << seed;

		BeamCrypto_NonceGenerator ng2;
		BeamCrypto_NonceGenerator_Init(&ng2, szSalt, sizeof(szSalt), &Ecc2BC(seed));

		for (int j = 0; j < 10; j++)
		{
			ECC::Scalar::Native sk1, sk2;
			ng1 >> sk1;
			BeamCrypto_NonceGenerator_NextScalar(&ng2, &sk2.get_Raw());

			verify_test(sk1 == sk2);
		}
	}
}

void TestOracle()
{
	for (int i = 0; i < 3; i++)
	{
		ECC::Oracle o1;
		BeamCrypto_Oracle o2;
		BeamCrypto_Oracle_Init(&o2);

		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < 3; k++)
			{
				ECC::Scalar::Native sk1, sk2;
				o1 >> sk1;
				BeamCrypto_Oracle_NextScalar(&o2, &sk2.get_Raw());

				verify_test(sk1 == sk2);
			}

			ECC::Hash::Value val;
			SetRandom(val);

			o1 << val;
			BeamCrypto_Oracle_Expose(&o2, val.m_pData, val.nBytes);
		}
	}
}

void TestCoin(const CoinID& cid, Key::IKdf& kdf, const BeamCrypto_Kdf& kdf2)
{
	ECC::Hash::Value hv1, hv2;
	cid.get_Hash(hv1);

	BeamCrypto_CoinID cid2;
	cid2.m_Idx = cid.m_Idx;
	cid2.m_Type = cid.m_Type;
	cid2.m_SubIdx = cid.m_SubIdx;
	cid2.m_Amount = cid.m_Value;
	cid2.m_AssetID = cid.m_AssetID;

	BeamCrypto_CoinID_getHash(&cid2, &Ecc2BC(hv2));

	verify_test(hv1 == hv2);

	uint8_t nScheme;
	uint32_t nSubKey;
	bool bChildKdf2 = !!BeamCrypto_CoinID_getSchemeAndSubkey(&cid2, &nScheme, &nSubKey);

	verify_test(cid.get_Scheme() == nScheme);

	uint32_t iChild;
	bool bChildKdf = cid.get_ChildKdfIndex(iChild);
	verify_test(bChildKdf == bChildKdf2);

	if (bChildKdf) {
		verify_test(nSubKey == iChild);
	}

	// keys and commitment
	ECC::Scalar::Native sk1, sk2;
	ECC::Point comm1, comm2;

	ECC::Key::IKdf* pChildKdf = &kdf;
	ECC::HKdf hkdfC;

	if (bChildKdf)
	{
		hkdfC.GenerateChild(kdf, iChild);
		pChildKdf = &hkdfC;
	}

	CoinID::Worker(cid).Create(sk1, comm1, *pChildKdf);

	BeamCrypto_CoinID_getSkComm(&kdf2, &cid2, &sk2.get_Raw(), &Ecc2BC(comm2));

	verify_test(sk1 == sk2);
	verify_test(comm1 == comm2);

	if (CoinID::Scheme::V1 != nScheme)
		return;

	// Generate multi-party output

	Output outp;
	outp.m_Commitment = comm1;

	ECC::HKdf kdfDummy;
	ECC::Scalar::Native skDummy;
	outp.Create(g_hFork, skDummy, kdfDummy, cid, kdf, Output::OpCode::Mpc_1); // Phase 1
	assert(outp.m_pConfidential);

	BeamCrypto_RangeProof rp;
	rp.m_pKdf = &kdf2;
	rp.m_Cid = cid2;
	rp.m_pT[0] = Ecc2BC(outp.m_pConfidential->m_Part2.m_T1);
	rp.m_pT[1] = Ecc2BC(outp.m_pConfidential->m_Part2.m_T2);
	rp.m_pKExtra = nullptr;
	ZeroObject(rp.m_TauX);

	verify_test(BeamCrypto_RangeProof_Calculate(&rp)); // Phase 2

	Ecc2BC(outp.m_pConfidential->m_Part2.m_T1) = rp.m_pT[0];
	Ecc2BC(outp.m_pConfidential->m_Part2.m_T2) = rp.m_pT[1];

	ECC::Scalar::Native tauX;
	tauX.get_Raw() = rp.m_TauX;
	outp.m_pConfidential->m_Part3.m_TauX = tauX;

	outp.Create(g_hFork, skDummy, kdfDummy, cid, kdf, Output::OpCode::Mpc_2); // Phase 3

	ECC::Point::Native comm;
	verify_test(outp.IsValid(g_hFork, comm));

	CoinID cid3;
	verify_test(outp.Recover(g_hFork, kdf, cid3));

	verify_test(cid == cid3);

}

void TestCoins()
{
	ECC::HKdf hkdf;
	BeamCrypto_Kdf kdf2;

	ECC::Hash::Value hv;
	SetRandom(hv);

	hkdf.Generate(hv);
	BeamCrypto_Kdf_Init(&kdf2, &Ecc2BC(hv));

	for (int i = 0; i < 3; i++)
	{
		CoinID cid;
		SetRandomOrd(cid.m_Idx);
		SetRandomOrd(cid.m_Type);
		SetRandomOrd(cid.m_Value);

		for (int iAsset = 0; iAsset < 2; iAsset++)
		{
			if (iAsset)
				SetRandomOrd(cid.m_AssetID);
			else
				cid.m_AssetID = 0;

			for (int iCh = 0; iCh < 2; iCh++)
			{
				uint32_t iChild;
				if (iCh)
				{
					SetRandomOrd(iChild);
					iChild &= (1U << 24) - 1;
				}
				else
					iChild = 0;

				cid.set_Subkey(iChild);
				TestCoin(cid, hkdf, kdf2);

				cid.set_Subkey(iChild, CoinID::Scheme::V0);
				TestCoin(cid, hkdf, kdf2);

				cid.set_Subkey(iChild, CoinID::Scheme::BB21);
				TestCoin(cid, hkdf, kdf2);
			}
		}
	}
}

void TestKdf()
{
	ECC::HKdf hkdf;
	BeamCrypto_Kdf kdf2;

	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value hv;
		SetRandom(hv);

		if (i)
		{
			uint32_t iChild;
			SetRandomOrd(iChild);

			hkdf.GenerateChild(hkdf, iChild);
			BeamCrypto_Kdf_getChild(&kdf2, iChild, &kdf2);
		}
		else
		{
			hkdf.Generate(hv);
			BeamCrypto_Kdf_Init(&kdf2, &Ecc2BC(hv));
		}

		for (int j = 0; j < 5; j++)
		{
			SetRandom(hv);

			ECC::Scalar::Native sk1, sk2;

			hkdf.DerivePKey(sk1, hv);
			BeamCrypto_Kdf_Derive_PKey(&kdf2, &Ecc2BC(hv), &sk2.get_Raw());
			verify_test(sk1 == sk2);

			hkdf.DeriveKey(sk1, hv);
			BeamCrypto_Kdf_Derive_SKey(&kdf2, &Ecc2BC(hv), &sk2.get_Raw());
			verify_test(sk1 == sk2);
		}
	}
}

void TestSignature()
{
	for (int i = 0; i < 5; i++)
	{
		ECC::Hash::Value msg;
		ECC::Scalar::Native sk;
		SetRandom(msg);
		SetRandom(sk);

		ECC::Point::Native pkN = ECC::Context::get().G * sk;
		ECC::Point pk = pkN;

		BeamCrypto_Signature sig2;
		BeamCrypto_Signature_Sign(&sig2, &Ecc2BC(msg), &sk.get_Raw());

		verify_test(BeamCrypto_Signature_IsValid_Gej(&sig2, &Ecc2BC(msg), &pkN.get_Raw()));
		verify_test(BeamCrypto_Signature_IsValid_Pt(&sig2, &Ecc2BC(msg), &Ecc2BC(pk)));

		ECC::Signature sig1;
		Ecc2BC(sig1.m_NoncePub) = sig2.m_NoncePub;
		Ecc2BC(sig1.m_k.m_Value) = sig2.m_k;

		verify_test(sig1.IsValid(msg, pkN));

		// tamper with sig
		sig2.m_k.m_pVal[0] ^= 12;
		verify_test(!BeamCrypto_Signature_IsValid_Gej(&sig2, &Ecc2BC(msg), &pkN.get_Raw()));
		verify_test(!BeamCrypto_Signature_IsValid_Pt(&sig2, &Ecc2BC(msg), &Ecc2BC(pk)));
	}
}

void TestKrn()
{
	for (int i = 0; i < 3; i++)
	{
		TxKernelStd krn1;
		SetRandomOrd(krn1.m_Fee);
		SetRandomOrd(krn1.m_Height.m_Min);
		SetRandomOrd(krn1.m_Height.m_Max);
		std::setmax(krn1.m_Height.m_Max, krn1.m_Height.m_Min);

		ECC::Scalar::Native sk;
		SetRandom(sk);
		krn1.Sign(sk);

		BeamCrypto_TxKernel krn2;
		krn2.m_Fee = krn1.m_Fee;
		krn2.m_hMin = krn1.m_Height.m_Min;
		krn2.m_hMax = krn1.m_Height.m_Max;
		krn2.m_Commitment = Ecc2BC(krn1.m_Commitment);
		krn2.m_Signature.m_k = Ecc2BC(krn1.m_Signature.m_k.m_Value);
		krn2.m_Signature.m_NoncePub = Ecc2BC(krn1.m_Signature.m_NoncePub);

		verify_test(BeamCrypto_TxKernel_IsValid(&krn2));

		ECC::Hash::Value msg;
		BeamCrypto_TxKernel_getID(&krn2, &Ecc2BC(msg));
		verify_test(msg == krn1.m_Internal.m_ID);

		// tamper
		krn2.m_Fee++;
		verify_test(!BeamCrypto_TxKernel_IsValid(&krn2));
	}
}

void TestPKdfExport()
{
	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value hv;
		SetRandom(hv);

		ECC::HKdf hkdf;
		hkdf.Generate(hv);

		BeamCrypto_KeyKeeper kk;
		BeamCrypto_Kdf_Init(&kk.m_MasterKey, &Ecc2BC(hv));

		for (int j = 0; j < 3; j++)
		{
			ECC::HKdf hkdfChild;
			ECC::HKdf* pKdf1 = &hkdfChild;

			BeamCrypto_KdfPub pkdf2;

			if (j)
			{
				uint32_t iChild;
				SetRandomOrd(iChild);

				BeamCrypto_KeyKeeper_GetPKdf(&kk, &pkdf2, &iChild);
				hkdfChild.GenerateChild(hkdf, iChild);
			}
			else
			{
				pKdf1 = &hkdf;
				BeamCrypto_KeyKeeper_GetPKdf(&kk, &pkdf2, nullptr);
			}

			ECC::HKdfPub::Packed p;
			Ecc2BC(p.m_Secret) = pkdf2.m_Secret;
			Ecc2BC(p.m_PkG) = pkdf2.m_CoFactorG;
			Ecc2BC(p.m_PkJ) = pkdf2.m_CoFactorJ;

			ECC::HKdfPub hkdf2;
			verify_test(hkdf2.Import(p));

			// Check the match between directly derived pKdf1 and imported hkdf2
			// Key::IPKdf::IsSame() is not a comprehensive test, it won't check point images (too expensive)

			ECC::Point::Native pt1, pt2;
			pKdf1->DerivePKeyG(pt1, hv);
			hkdf2.DerivePKeyG(pt2, hv);
			verify_test(pt1 == pt2);

			pKdf1->DerivePKeyJ(pt1, hv);
			hkdf2.DerivePKeyJ(pt2, hv);
			verify_test(pt1 == pt2);
		}
	}
}


struct KeyKeeperHwEmu
	:public wallet::PrivateKeyKeeper_AsyncNotify
{
	BeamCrypto_KeyKeeper m_Ctx;
	Key::IPKdf::Ptr m_pOwnerKey; // cached

	bool get_PKdf(Key::IPKdf::Ptr&, const uint32_t*);
	bool get_OwnerKey();

	static void CidCvt(BeamCrypto_CoinID&, const CoinID&);

	static void TxImport(BeamCrypto_TxCommon&, const Method::TxCommon&, std::vector<BeamCrypto_CoinID>&);
	static void TxExport(Method::TxCommon&, const BeamCrypto_TxCommon&);


#define THE_MACRO(method) \
        virtual Status::Type InvokeSync(Method::method& m) override;

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO
};

bool KeyKeeperHwEmu::get_PKdf(Key::IPKdf::Ptr& pRes, const uint32_t* pChild)
{
	BeamCrypto_KdfPub pkdf;
	BeamCrypto_KeyKeeper_GetPKdf(&m_Ctx, &pkdf, pChild);

	ECC::HKdfPub::Packed p;
	Ecc2BC(p.m_Secret) = pkdf.m_Secret;
	Ecc2BC(p.m_PkG) = pkdf.m_CoFactorG;
	Ecc2BC(p.m_PkJ) = pkdf.m_CoFactorJ;

	pRes = std::make_unique<ECC::HKdfPub>();

	if (Cast::Up<ECC::HKdfPub>(*pRes).Import(p))
		return true;

	pRes.reset();
	return false;
}

bool KeyKeeperHwEmu::get_OwnerKey()
{
	return m_pOwnerKey || get_PKdf(m_pOwnerKey, nullptr);
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::get_Kdf& m)
{
	if (m.m_Root)
	{
		get_OwnerKey();
		m.m_pPKdf = m_pOwnerKey;
	}
	else
		get_PKdf(m.m_pPKdf, &m.m_iChild);

	return m.m_pPKdf ? Status::Success : Status::Unspecified;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::get_NumSlots& m)
{
	m.m_Count = 64;
	return Status::Success;
}

void KeyKeeperHwEmu::CidCvt(BeamCrypto_CoinID& cid2, const CoinID& cid)
{
	cid2.m_Idx = cid.m_Idx;
	cid2.m_SubIdx = cid.m_SubIdx;
	cid2.m_Type = cid.m_Type;
	cid2.m_AssetID = cid.m_AssetID;
	cid2.m_Amount = cid.m_Value;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::CreateOutput& m)
{
	if (m.m_hScheme < Rules::get().pForks[1].m_Height)
		return Status::NotImplemented;

	if (!get_OwnerKey())
		return Status::Unspecified;

	Output::Ptr pOutp = std::make_unique<Output>();

	ECC::Point::Native comm;
	get_Commitment(comm, m.m_Cid);
	pOutp->m_Commitment = comm;

	// rangeproof
	ECC::Scalar::Native skDummy;
	ECC::HKdf kdfDummy;

	pOutp->Create(g_hFork, skDummy, kdfDummy, m.m_Cid, *m_pOwnerKey, Output::OpCode::Mpc_1);
	assert(pOutp->m_pConfidential);

	BeamCrypto_RangeProof rp;
	rp.m_pKdf = &m_Ctx.m_MasterKey;
	CidCvt(rp.m_Cid, m.m_Cid);

	rp.m_pT[0] = Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T1);
	rp.m_pT[1] = Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T2);
	rp.m_pKExtra = nullptr;
	ZeroObject(rp.m_TauX);

	if (!BeamCrypto_RangeProof_Calculate(&rp)) // Phase 2
		return Status::Unspecified;

	Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T1) = rp.m_pT[0];
	Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T2) = rp.m_pT[1];

	ECC::Scalar::Native tauX;
	tauX.get_Raw() = rp.m_TauX;
	pOutp->m_pConfidential->m_Part3.m_TauX = tauX;

	pOutp->Create(g_hFork, skDummy, kdfDummy, m.m_Cid, *m_pOwnerKey, Output::OpCode::Mpc_2); // Phase 3

	m.m_pResult.swap(pOutp);

	return Status::Success;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::SignReceiver& m)
{
	return Status::NotImplemented;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::SignSender& m)
{
	return Status::NotImplemented;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::SignSplit& m)
{
	std::vector<BeamCrypto_CoinID> vCvt;
	BeamCrypto_TxCommon tx2;
	TxImport(tx2, m, vCvt);

	int nRet = BeamCrypto_KeyKeeper_SignTx_Split(&m_Ctx, &tx2);

	if (BeamCrypto_KeyKeeper_Status_Ok == nRet)
		TxExport(m, tx2);

	return static_cast<Status::Type>(nRet);
}

void KeyKeeperHwEmu::TxImport(BeamCrypto_TxCommon& tx2, const Method::TxCommon& m, std::vector<BeamCrypto_CoinID>& v)
{
	tx2.m_Ins = static_cast<unsigned int>(m.m_vInputs.size());
	tx2.m_Outs = static_cast<unsigned int>(m.m_vOutputs.size());

	v.resize(tx2.m_Ins + tx2.m_Outs);
	if (!v.empty())
	{
		for (size_t i = 0; i < v.size(); i++)
			CidCvt(v[i], (i < tx2.m_Ins) ? m.m_vInputs[i] : m.m_vOutputs[i - tx2.m_Ins]);

		tx2.m_pIns = &v.front();
		tx2.m_pOuts = &v.front() + tx2.m_Ins;
	}

	// kernel
	assert(m.m_pKernel);
	tx2.m_Krn.m_Fee = m.m_pKernel->m_Fee;
	tx2.m_Krn.m_hMin = m.m_pKernel->m_Height.m_Min;
	tx2.m_Krn.m_hMax = m.m_pKernel->m_Height.m_Max;

	tx2.m_Krn.m_Commitment = Ecc2BC(m.m_pKernel->m_Commitment);
	tx2.m_Krn.m_Signature.m_NoncePub = Ecc2BC(m.m_pKernel->m_Signature.m_NoncePub);
	tx2.m_Krn.m_Signature.m_k = Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value);

	// offset
	ECC::Scalar kOffs(m.m_kOffset);
	tx2.m_kOffset = Ecc2BC(kOffs.m_Value);
}

void KeyKeeperHwEmu::TxExport(Method::TxCommon& m, const BeamCrypto_TxCommon& tx2)
{
	// kernel
	assert(m.m_pKernel);
	m.m_pKernel->m_Fee = tx2.m_Krn.m_Fee;
	m.m_pKernel->m_Height.m_Min = tx2.m_Krn.m_hMin;
	m.m_pKernel->m_Height.m_Max = tx2.m_Krn.m_hMax;

	Ecc2BC(m.m_pKernel->m_Commitment) = tx2.m_Krn.m_Commitment;
	Ecc2BC(m.m_pKernel->m_Signature.m_NoncePub) = tx2.m_Krn.m_Signature.m_NoncePub;
	Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value) = tx2.m_Krn.m_Signature.m_k;

	m.m_pKernel->UpdateID();

	// offset
	ECC::Scalar kOffs;
	Ecc2BC(kOffs.m_Value) = tx2.m_kOffset;
	m.m_kOffset = kOffs;
}


struct KeyKeeperStd
	:public wallet::LocalPrivateKeyKeeperStd
{
	bool m_Trustless = true;

	using LocalPrivateKeyKeeperStd::LocalPrivateKeyKeeperStd;
	virtual bool IsTrustless() override { return m_Trustless; }
};



struct KeyKeeperWrap
{
	KeyKeeperHwEmu m_kkEmu;
	KeyKeeperStd m_kkStd; // for comparison

	KeyKeeperWrap(const Key::IKdf::Ptr& pKdf)
		:m_kkStd(pKdf)
	{
	}

	static CoinID& Add(std::vector<CoinID>& vec, Amount val = 0);

	void ExportTx(Transaction& tx, const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2);
	void TestTx(const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2);

	void TextKeyKeeperSplit();

	static void TestSameKrn(TxKernelStd& k1, TxKernelStd& k2)
	{
		k1.UpdateID();
		k2.UpdateID();
		verify_test(k1.m_Internal.m_ID == k2.m_Internal.m_ID);
		verify_test(k1.m_Signature.m_k == k2.m_Signature.m_k);
	}

	template <typename TMethod>
	int InvokeOnBoth(TMethod& m)
	{
		TxKernelStd::Ptr pKrn;
		m.m_pKernel->Clone((TxKernel::Ptr&) pKrn);
		ECC::Scalar::Native kOffs = m.m_kOffset;

		int n1 = m_kkEmu.InvokeSync(m);

		m.m_pKernel.swap(pKrn);
		std::swap(m.m_kOffset, kOffs);

		int n2 = m_kkStd.InvokeSync(m);

		verify_test(n1 == n2);

		if (KeyKeeperHwEmu::Status::Success == n1)
		{
			verify_test(kOffs == m.m_kOffset);
			TestSameKrn(*pKrn, *m.m_pKernel);
		}

		return n1;
	}
};

CoinID& KeyKeeperWrap::Add(std::vector<CoinID>& vec, Amount val)
{
	CoinID& ret = vec.emplace_back();

	uint64_t nIdx;
	SetRandomOrd(nIdx);

	ret = CoinID(val, nIdx, Key::Type::Regular);
	return ret;
}

void KeyKeeperWrap::ExportTx(Transaction& tx, const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2)
{
	tx.m_vInputs.resize(tx2.m_vInputs.size());

	ECC::Point::Native pt;

	for (unsigned int i = 0; i < tx.m_vInputs.size(); i++)
	{
		Input::Ptr& pInp = tx.m_vInputs[i];
		pInp = std::make_unique<Input>();

		m_kkEmu.get_Commitment(pt, tx2.m_vInputs[i]);
		pInp->m_Commitment = pt;
	}

	tx.m_vOutputs.resize(tx2.m_vOutputs.size());

	for (unsigned int i = 0; i < tx.m_vOutputs.size(); i++)
	{
		KeyKeeperHwEmu::Method::CreateOutput m;
		m.m_hScheme = g_hFork;
		m.m_Cid = tx2.m_vOutputs[i];
		
		verify_test(m_kkEmu.InvokeSync(m) == KeyKeeperHwEmu::Status::Success);
		assert(m.m_pResult);

		tx.m_vOutputs[i].swap(m.m_pResult);
	}

	// kernel
	assert(tx2.m_pKernel);
	TxKernel::Ptr& pKrn = tx.m_vKernels.emplace_back();
	tx2.m_pKernel->Clone(pKrn);

	// offset
	tx.m_Offset = tx2.m_kOffset;

	tx.Normalize();
}

void KeyKeeperWrap::TestTx(const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2)
{
	Transaction tx;
	ExportTx(tx, tx2);

	Transaction::Context::Params pars;
	Transaction::Context ctx(pars);
	ctx.m_Height.m_Min = g_hFork;
	verify_test(tx.IsValid(ctx));
}

void KeyKeeperWrap::TextKeyKeeperSplit()
{
	wallet::IPrivateKeyKeeper2::Method::SignSplit m;

	Add(m.m_vInputs, 55);
	Add(m.m_vInputs, 16);

	Add(m.m_vOutputs, 12);
	Add(m.m_vOutputs, 13);
	Add(m.m_vOutputs, 14);

	m.m_pKernel = std::make_unique<TxKernelStd>();
	m.m_pKernel->m_Height.m_Min = g_hFork;
	m.m_pKernel->m_Height.m_Max = g_hFork + 40;
	m.m_pKernel->m_Fee = 30; // Incorrect balance (funds missing)

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_pKernel->m_Fee = 32; // ok
	
	m.m_vOutputs[0].set_Subkey(0, CoinID::Scheme::V0); // weak output scheme
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_vOutputs[0].set_Subkey(0, CoinID::Scheme::BB21); // weak output scheme
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_vOutputs[0].set_Subkey(12); // outputs to a child key
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_vOutputs[0].set_Subkey(0); // ok

	m.m_vInputs[0].set_Subkey(14, CoinID::Scheme::V0); // weak input scheme
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m_kkEmu.m_Ctx.m_AllowWeakInputs = 1;
	m_kkStd.m_Trustless = false; // no explicit flag for weak inputs, just switch to trusted mode
	verify_test(InvokeOnBoth(m) == KeyKeeperHwEmu::Status::Success); // should work now

	TestTx(m);

	// add asset
	Add(m.m_vInputs, 16).m_AssetID = 12;
	Add(m.m_vOutputs, 16).m_AssetID = 13;

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // different assets mixed (not allowed)

	m.m_vOutputs.back().m_AssetID = 12;
	m.m_vOutputs.back().m_Value = 15;
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // asset balance mismatch

	m.m_vOutputs.back().m_Value = 16;
	m.m_kOffset = Zero; // m_kkStd assumes it's 0-initialized
	verify_test(InvokeOnBoth(m) == KeyKeeperHwEmu::Status::Success); // ok

	TestTx(m);

	m_kkEmu.m_Ctx.m_AllowWeakInputs = 0;
	m_kkStd.m_Trustless = true;
}

void TextKeyKeeperTxs()
{
	ECC::Hash::Value hv;
	SetRandom(hv);

	Key::IKdf::Ptr pKdf;
	ECC::HKdf::Create(pKdf, hv);

	KeyKeeperWrap kkw(pKdf);

	kkw.m_kkEmu.m_Ctx.m_AllowWeakInputs = 0;
	BeamCrypto_Kdf_Init(&kkw.m_kkEmu.m_Ctx.m_MasterKey, &Ecc2BC(hv));

	kkw.TextKeyKeeperSplit();
}

int main()
{
	Rules::get().CA.Enabled = true;
	Rules::get().pForks[1].m_Height = g_hFork;
	Rules::get().pForks[2].m_Height = g_hFork;

	InitContext();

	TestMultiMac();
	TestNonceGen();
	TestOracle();
	TestKdf();
	TestCoins();
	TestSignature();
	TestKrn();
	TestPKdfExport();
	TextKeyKeeperTxs();

    return g_TestsFailed ? -1 : 0;
}