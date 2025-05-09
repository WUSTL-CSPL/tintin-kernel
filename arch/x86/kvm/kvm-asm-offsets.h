#ifndef __KVM_ASM_OFFSETS_H__
#define __KVM_ASM_OFFSETS_H__
/*
 * DO NOT MODIFY.
 *
 * This file was generated by Kbuild
 */


#define SVM_vcpu_arch_regs 280 /* offsetof(struct vcpu_svm, vcpu.arch.regs) */
#define SVM_current_vmcb 6744 /* offsetof(struct vcpu_svm, current_vmcb) */
#define SVM_spec_ctrl 6792 /* offsetof(struct vcpu_svm, spec_ctrl) */
#define SVM_vmcb01 6712 /* offsetof(struct vcpu_svm, vmcb01) */
#define KVM_VMCB_pa 8 /* offsetof(struct kvm_vmcb_info, pa) */
#define SD_save_area_pa 40 /* offsetof(struct svm_cpu_data, save_area_pa) */

#define VMX_spec_ctrl 6928 /* offsetof(struct vcpu_vmx, spec_ctrl) */

#endif
