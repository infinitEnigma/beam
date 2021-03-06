// Copyright 2019 The Beam Team
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

#pragma once

#include "common.h"
#include <boost/intrusive/list.hpp>

namespace beam::wallet
{
    using WalletIDKey = uint64_t;

    struct IPrivateKeyKeeper
    {
        struct Handler
        {
            using Ptr = Handler*;

            virtual void onShowKeyKeeperMessage() = 0;
            virtual void onHideKeyKeeperMessage() = 0;
            virtual void onShowKeyKeeperError(const std::string&) = 0;
        };
    };

    //
    // Interface to master key storage. HW wallet etc.
    // Only public info should cross its boundary.
    //
    struct IPrivateKeyKeeper2
    {
        typedef std::shared_ptr<IPrivateKeyKeeper2> Ptr;

        struct Slot
        {
            typedef uint32_t Type;
            static const Type Invalid;
        };

        struct Status
        {
            typedef int Type;

            static const Type Success = 0;
            static const Type InProgress = -1;
            static const Type Unspecified = 1;
            static const Type UserAbort = 2;
            static const Type NotImplemented = 3;
        };

        struct Handler
        {
            typedef std::shared_ptr<Handler> Ptr;

            virtual ~Handler() {}
            virtual void OnDone(Status::Type) = 0;
        };

        struct Method
        {
            struct get_Kdf
            {
                Key::Index m_iChild;
                bool m_Root; // if true the m_iChild is ignored

                Key::IKdf::Ptr m_pKdf; // only for trusted host
                Key::IPKdf::Ptr m_pPKdf;

                void From(const CoinID&);
            };

            struct get_NumSlots {
                Slot::Type m_Count;
            };

            struct CreateOutput {
                Height m_hScheme; // scheme prior to Fork1 isn't supported for trustless wallet
                CoinID m_Cid; // weak schemes (V0, BB21) isn't supported for trustless wallet
                Output::Ptr m_pResult;
            };

            struct InOuts
            {
                std::vector<CoinID> m_vInputs;
                std::vector<CoinID> m_vOutputs;
            };

            struct TxCommon :public InOuts
            {
                TxKernelStd::Ptr m_pKernel;
                ECC::Scalar::Native m_kOffset;

                bool m_NonConventional = false; // trusted mode only. Needed for synthetic txs, such as multisig, lock, swap and etc.
                // Balance doesn't have to match send/receive semantics, payment confirmation is neither generated nor verified
            };

            struct TxMutual :public TxCommon
            {
                // for mutually-constructed kernel
                PeerID m_Peer;
                WalletIDKey m_MyIDKey; // Must set for trustless wallet
                ECC::Signature m_PaymentProofSignature;
            };

            struct SignReceiver :public TxMutual {
            };

            struct SignSender :public TxMutual {
                Slot::Type m_Slot;
                ECC::Hash::Value m_UserAgreement; // set to Zero on 1st invocation
                PeerID m_MyID; // set in legacy mode (where it was sbbs pubkey) instead of m_MyIDKey. Otherwise it'll be set automatically.
            };

            struct SignSplit :public TxCommon {
                // send funds to yourself. in/out difference must be equal to fee
            };

        };

#define KEY_KEEPER_METHODS(macro) \
		macro(get_Kdf) \
		macro(get_NumSlots) \
		macro(CreateOutput) \
		macro(SignReceiver) \
		macro(SignSender) \
		macro(SignSplit) \


#define THE_MACRO(method) \
			virtual Status::Type InvokeSync(Method::method&); \
			virtual void InvokeAsync(Method::method&, const Handler::Ptr&) = 0;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

        virtual ~IPrivateKeyKeeper2() {}

        // synthetic functions (in terms of underlying ones)
        Status::Type get_Commitment(ECC::Point::Native&, const CoinID&);

    private:
        struct HandlerSync;

        template <typename TMethod>
        Status::Type InvokeSyncInternal(TMethod& m);
    };


	class PrivateKeyKeeper_AsyncNotify // by default emulates async calls by synchronous, and then asynchronously posts completion status
		:public IPrivateKeyKeeper2
	{
    protected:

		io::AsyncEvent::Ptr m_pNewOut;

        struct Task
            :public boost::intrusive::list_base_hook<>
        {
            typedef std::unique_ptr<Task> Ptr;

            Handler::Ptr m_pHandler;
            Status::Type m_Status;

            virtual ~Task() {} // necessary for derived classes, that may add arbitrary data memebers
        };

		struct TaskList
			:public boost::intrusive::list<Task>
		{
            void Pop(Task::Ptr&);
            bool Push(Task::Ptr&); // returns if was empty
            void Clear();

			~TaskList() { Clear(); }
		};

		TaskList m_queIn;
		TaskList m_queOut;

        void EnsureEvtOut();
        void PushOut(Task::Ptr& p);
        void PushOut(Status::Type, const Handler::Ptr&);
        
        virtual void OnNewOut();
        static void CallNewOut(TaskList&);

    public:

#define THE_MACRO(method) \
		void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

		KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	};

	class ThreadedPrivateKeyKeeper
		:public PrivateKeyKeeper_AsyncNotify
	{
        IPrivateKeyKeeper2::Ptr m_pKeyKeeper;

		std::thread m_Thread;
		bool m_Run = true;

		std::mutex m_MutexIn;
		std::condition_variable m_NewIn;

		std::mutex m_MutexOut;

        struct Task
            :public PrivateKeyKeeper_AsyncNotify::Task
        {
            virtual void Exec(IPrivateKeyKeeper2&) = 0;
        };

		TaskList m_queIn;

        void PushIn(Task::Ptr& p);
        void Thread();

        virtual void OnNewOut() override;

    public:

        ThreadedPrivateKeyKeeper(const IPrivateKeyKeeper2::Ptr& p);
        ~ThreadedPrivateKeyKeeper();

		template <typename TMethod>
        void InvokeAsyncInternal(TMethod& m, const Handler::Ptr& pHandler);

#define THE_MACRO(method) \
		void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

		KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	};

}
